#pragma once

#include "thirdparty/indicators/dynamic_progress.hpp"
#include "thirdparty/indicators/progress_bar.hpp"

#include "output.hpp"
#include "validate.hpp"

extern "C" {
    #include <stdio.h>
    #include <string.h>

    // unix-specific
    #ifndef _WIN32
    #include <sys/time.h>
    #include <unistd.h>
    #endif

    #include <curl/curl.h>
    #include <archive.h>
}

#define PREFIX_LENGTH 25

namespace mamba
{

    class DownloadTarget
    {
    public:
        static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *self)
        {
            auto* s = (DownloadTarget*)self;
            s->m_file.write(ptr, size * nmemb);
            return size * nmemb;
        }

        DownloadTarget() = default;

        int progress_callback(void*, curl_off_t total_to_download, curl_off_t now_downloaded, curl_off_t, curl_off_t)
        {
            if (Context::instance().quiet || Context::instance().json)
            {
                return 0;
            }

            auto now = std::chrono::steady_clock::now();
            if (now - m_progress_throttle_time < std::chrono::milliseconds(150))
            {
                return 0;
            }
            m_progress_throttle_time = now;


            if (total_to_download != 0 && now_downloaded == 0 && m_expected_size != 0)
            {
                now_downloaded = total_to_download;
                total_to_download = m_expected_size;
            }

            if ((total_to_download != 0 || m_expected_size != 0) && now_downloaded != 0)
            {
                double perc = double(now_downloaded) / double(total_to_download);
                std::stringstream postfix;
                to_human_readable_filesize(postfix, now_downloaded);
                postfix << " / ";
                to_human_readable_filesize(postfix, total_to_download);
                postfix << " (";
                to_human_readable_filesize(postfix, get_speed(), 2);
                postfix << "/s)";
                m_progress_bar.set_progress(perc * 100.);
                m_progress_bar.set_option(indicators::option::PostfixText{postfix.str()});
            }
            if (now_downloaded == 0 && total_to_download != 0)
            {
                std::stringstream postfix;
                to_human_readable_filesize(postfix, total_to_download);
                postfix << " / ?? (";
                to_human_readable_filesize(postfix, get_speed(), 2);
                postfix << "/s)";
                m_progress_bar.set_progress(0);
                m_progress_bar.set_option(indicators::option::PostfixText{postfix.str()});
            }
            return 0;
        }

        DownloadTarget(const std::string& name, const std::string& url, const std::string& filename)
            : m_name(name), m_filename(filename)
        {
            m_file = std::ofstream(filename, std::ios::binary);

            m_target = curl_easy_init();

            curl_easy_setopt(m_target, CURLOPT_URL, url.c_str());

            curl_easy_setopt(m_target, CURLOPT_HEADERFUNCTION, &DownloadTarget::header_callback);
            curl_easy_setopt(m_target, CURLOPT_HEADERDATA, this);

            curl_easy_setopt(m_target, CURLOPT_WRITEFUNCTION, &DownloadTarget::write_callback);
            curl_easy_setopt(m_target, CURLOPT_WRITEDATA, this);

            m_headers = nullptr;
            if (ends_with(url, ".json"))
            {
                curl_easy_setopt(m_target, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, compress, identity");
                m_headers = curl_slist_append(m_headers, "Content-Type: application/json");
            }
            curl_easy_setopt(m_target, CURLOPT_HTTPHEADER, m_headers);
            curl_easy_setopt(m_target, CURLOPT_VERBOSE, Context::instance().verbosity != 0);
        }

        void set_mod_etag_headers(const nlohmann::json& mod_etag)
        {
            auto to_header = [](const std::string& key, const std::string& value) {
                return std::string(key + ": " + value);
            };

            if (mod_etag.find("_etag") != mod_etag.end()) 
            {
                m_headers = curl_slist_append(m_headers, to_header("If-None-Match", mod_etag["_etag"]).c_str());
            }
            if (mod_etag.find("_mod") != mod_etag.end()) 
            {
                m_headers = curl_slist_append(m_headers, to_header("If-Modified-Since", mod_etag["_mod"]).c_str());
            }
        }

        void set_progress_bar(Output::ProgressProxy progress_proxy)
        {
            using namespace std::placeholders;
            m_has_progress_bar = true;
            m_progress_bar = progress_proxy;
            curl_easy_setopt(m_target, CURLOPT_XFERINFOFUNCTION, &DownloadTarget::progress_callback);
            curl_easy_setopt(m_target, CURLOPT_XFERINFODATA, this);
            curl_easy_setopt(m_target, CURLOPT_NOPROGRESS, 0L);
        }

        void set_expected_size(std::size_t size)
        {
            m_expected_size = size;
        }

        static size_t header_callback(char *buffer, size_t size, size_t nitems, void *self)
        {
            auto* s = (DownloadTarget*)self;

            std::string_view header(buffer, size * nitems);
            auto colon_idx = header.find(':');
            if (colon_idx != std::string_view::npos)
            {
                std::string_view key, value;
                key = header.substr(0, colon_idx);
                colon_idx++;
                // remove spaces
                while (std::isspace(header[colon_idx]))
                {
                    ++colon_idx;
                }
                // remove \r\n header ending
                value = header.substr(colon_idx, header.size() - colon_idx - 2);
                if (key == "ETag")
                {
                    s->etag = std::string(value);
                }
                else if (key == "Cache-Control")
                {
                    s->cache_control = std::string(value);
                }
                else if (key == "Last-Modified")
                {
                    s->mod = std::string(value);
                }
            }
            return nitems * size;
        }

        const std::string& name() const
        {
            return m_name;
        }

        bool perform()
        {
            CURLcode res = curl_easy_perform(m_target);
            if (res != CURLE_OK)
            {
                throw std::runtime_error(curl_easy_strerror(res));
            }
            if (m_finalize_callback)
            {
                return m_finalize_callback();
            }
            else return true;
        }

        CURL* handle()
        {
            return m_target;
        }

        curl_off_t get_speed()
        {
            curl_off_t speed;
            CURLcode res = curl_easy_getinfo(m_target, CURLINFO_SPEED_DOWNLOAD_T, &speed);
            if (res == CURLE_OK)
            {
                return speed;
            }
            return 0;
        }

        template <class C>
        void set_finalize_callback(int (C::*cb)(), C* data)
        {
            m_finalize_callback = std::bind(cb, data);
        }

        bool finalize()
        {
            m_file.flush();

            char* effective_url = nullptr;
            curl_easy_getinfo(m_target, CURLINFO_RESPONSE_CODE, &http_status);
            curl_easy_getinfo(m_target, CURLINFO_EFFECTIVE_URL, &effective_url);
            curl_easy_getinfo(m_target, CURLINFO_SIZE_DOWNLOAD_T, &downloaded_size);

            LOG(INFO) << "Transfer finalized, status: " << http_status << " @ " << effective_url << " " << downloaded_size << " bytes";

            final_url = effective_url;
            if (m_finalize_callback)
            {
                return m_finalize_callback();
            }
            else
            {
                validate();
                if (m_has_progress_bar)
                {
                    m_progress_bar.mark_as_completed("Downloaded " + m_name);
                }
            }
            return true;
        }

        void validate()
        {
            if (m_expected_size)
            {
                curl_off_t dl_size;
                curl_easy_getinfo(m_target, CURLINFO_SIZE_DOWNLOAD_T, &dl_size);
                if (dl_size != m_expected_size)
                {
                    throw std::runtime_error("Download of " + m_name + " does not have expected size!");
                }
                validate::sha256(m_filename, dl_size, m_sha256);
            }
        }

        void set_sha256(const std::string& sha256)
        {
            m_sha256 = sha256;
        }

        ~DownloadTarget()
        {
            curl_easy_cleanup(m_target);
            curl_slist_free_all(m_headers);
        }

        int http_status;
        std::string final_url;
        curl_off_t downloaded_size;

        std::string etag, mod, cache_control;

    private:
        std::function<int()> m_finalize_callback;

        std::string m_name, m_filename;

        // validation
        std::size_t m_expected_size = 0;
        std::string m_sha256;

        std::chrono::steady_clock::time_point m_progress_throttle_time;

        CURL* m_target;
        curl_slist* m_headers;

        bool m_has_progress_bar = false;
        Output::ProgressProxy m_progress_bar;

        std::ofstream m_file;
    };

    class MultiDownloadTarget
    {
    public:
        MultiDownloadTarget()
        {
            m_handle = curl_multi_init();
            curl_multi_setopt(m_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                              Context::instance().max_parallel_downloads);
        }

        ~MultiDownloadTarget()
        {
            curl_multi_cleanup(m_handle);
        }

        void add(std::unique_ptr<DownloadTarget>& target)
        {
            if (!target) return;
            CURLMcode code = curl_multi_add_handle(m_handle, target->handle());
            if(code != CURLM_CALL_MULTI_PERFORM)
            {
                if(code != CURLM_OK)
                {
                    throw std::runtime_error(curl_multi_strerror(code));
                }
            }
            m_targets.push_back(target.get());
        }

        bool check_msgs()
        {
            int msgs_in_queue;
            CURLMsg *msg;

            while ((msg = curl_multi_info_read(m_handle, &msgs_in_queue))) {

                if (msg->data.result != CURLE_OK) {
                    char* effective_url = nullptr;
                    curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &effective_url);
                    std::stringstream err;
                    err << "Download error (" << msg->data.result << ") " <<
                           curl_easy_strerror(msg->data.result) << "[" << effective_url << "]";

                    throw std::runtime_error(err.str());
                }

                if (msg->msg != CURLMSG_DONE) {
                    // We are only interested in messages about finished transfers
                    continue;
                }

                DownloadTarget* current_target = nullptr;
                for (const auto& target : m_targets)
                {
                    if (target->handle() == msg->easy_handle)
                    {
                        current_target = target;
                        break;
                    }
                }

                if (!current_target)
                {
                    throw std::runtime_error("Could not find target associated with multi request");   
                }

                // flush file & finalize transfer
                current_target->finalize();
            }
            return true;
        }

        bool download(bool failfast)
        {
            LOG(INFO) << "Starting to download targets";

            int still_running, repeats = 0;
            const long max_wait_msecs = 400;
            do
            {
                CURLMcode code = curl_multi_perform(m_handle, &still_running);                

                if(code != CURLM_OK)
                {
                    throw std::runtime_error(curl_multi_strerror(code));
                }
                check_msgs();

                int numfds;
                code = curl_multi_wait(m_handle, NULL, 0, max_wait_msecs, &numfds);

                if (code != CURLM_OK)
                {
                    throw std::runtime_error(curl_multi_strerror(code));
                }

                if(!numfds)
                {
                    repeats++; // count number of repeated zero numfds
                    if(repeats > 1)
                    {
                    // wait 100 ms
                    #ifdef _WIN32
                        Sleep(100);
                    #else
                        // Portable sleep for platforms other than Windows.
                        struct timeval wait = { 0, 100 * 1000 };
                        (void) select(0, NULL, NULL, NULL, &wait);
                    #endif
                    }
                }
                else
                {
                    repeats = 0;
                }
            } while (still_running && !Context::instance().sig_interrupt);

            if (Context::instance().sig_interrupt)
            {
                std::cout << "Download interrupted" << std::endl;
                curl_multi_cleanup(m_handle);
            }
            return true;
        }

        std::vector<DownloadTarget*> m_targets;
        CURLM* m_handle;
    };

}