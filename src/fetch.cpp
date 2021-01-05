// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <string_view>
#include <thread>

#include "mamba/fetch.hpp"
#include "mamba/context.hpp"
#include "mamba/thread_utils.hpp"
#include "mamba/util.hpp"

namespace mamba
{
    // Fail if dl_ctx->fail_no_ranges is set and we get a 200 response
    size_t dl_header_cb(char* b, size_t l, size_t c, void* dl_v)
    {
        dlCtx* dl_ctx = (dlCtx*) dl_v;
        DownloadTarget::header_callback(b, l, c, dl_ctx->target);
        if (dl_ctx->fail_no_ranges)
        {
            long code = -1;
            curl_easy_getinfo(dl_ctx->curl, CURLINFO_RESPONSE_CODE, &code);
            if (code == 200)
            {
                dl_ctx->range_fail = 1;
                return 0;
            }
        }
        return zck_header_cb(b, l, c, dl_ctx->dl);
    }

    /*********************************
     * DownloadTarget implementation *
     *********************************/

    DownloadTarget::DownloadTarget(const std::string& name,
                                   const std::string& url,
                                   const std::string& filename,
                                   const std::string& zchunk_source)
        : m_name(name)
        , m_filename(filename)
        , m_url(url)
        , m_zchunk_source(zchunk_source)
        , m_zck_src(NULL)
        , m_is_zchunk(false)
        , m_zchunk_err(0)
    {
        m_file = std::ofstream(filename, std::ios::binary);

        m_handle = curl_easy_init();

        if (ends_with(url, ".zck"))
        {
            // target is a zchunk file
            m_is_zchunk = true;
            RESET_STATE(dl_range);
            RESET_STATE(dl_byte_range);
            RESET_STATE(dl_bytes);
            RESET_STATE(dl_header);
            RESET_STATE(init_zchunk_target);
            init_zchunk_target(m_url);
        }
        else
        {
            init_curl_target(url);
        }
    }

    bool DownloadTarget::is_zchunk()
    {
        return m_is_zchunk;
    }

    // Return 0 on error, -1 on 200 response (if fail_no_ranges),
    // and 1 on complete success
    bool DownloadTarget::dl_range(
        int& result, dlCtx* dl_ctx, const char* url, char* range, int is_chunk)
    {
#define VARIABLE(x) __dl_range_##x##__
        BEGIN(dl_range);

        if (dl_ctx == NULL || dl_ctx->dl == NULL)
        {
            free(range);
            LOG_ERROR << "Struct not defined";
            result = 0;
            RETURN(dl_range);
        }
        dl_ctx->target = this;

        VARIABLE(curl) = dl_ctx->curl;

        curl_easy_setopt(VARIABLE(curl), CURLOPT_URL, url);
        curl_easy_setopt(VARIABLE(curl), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(VARIABLE(curl), CURLOPT_HEADERFUNCTION, dl_header_cb);
        curl_easy_setopt(VARIABLE(curl), CURLOPT_HEADERDATA, dl_ctx);
        if (is_chunk)
        {
            curl_easy_setopt(VARIABLE(curl), CURLOPT_WRITEFUNCTION, zck_write_chunk_cb);
        }
        else
        {
            curl_easy_setopt(VARIABLE(curl), CURLOPT_WRITEFUNCTION, zck_write_zck_header_cb);
        }
        curl_easy_setopt(VARIABLE(curl), CURLOPT_WRITEDATA, dl_ctx->dl);
        curl_easy_setopt(VARIABLE(curl), CURLOPT_RANGE, range);
        YIELD(dl_range);  // this is when the CURL download will happen
        free(range);

        if (dl_ctx->range_fail)
        {
            result = -1;
            RETURN(dl_range);
        }

        long code;
        curl_easy_getinfo(VARIABLE(curl), CURLINFO_RESPONSE_CODE, &code);
        if (code != 206 && code != 200)
        {
            LOG_ERROR << "HTTP Error: " << code << " when downloading " << url;
            result = 0;
            RETURN(dl_range);
        }

        result = 1;

        END(dl_range);
#undef VARIABLE
    }

    bool DownloadTarget::dl_byte_range(
        int& result, dlCtx* dl_ctx, const char* url, int start, int end)
    {
#define VARIABLE(x) __dl_byte_range_##x##__
        BEGIN(dl_byte_range);

        VARIABLE(range) = NULL;
        zck_dl_reset(dl_ctx->dl);
        if (start > -1 && end > -1)
        {
            VARIABLE(range) = zck_get_range(start, end);
        }
        AWAIT(dl_byte_range, dl_range(result, dl_ctx, url, VARIABLE(range), 0));

        END(dl_byte_range);
#undef VARIABLE
    }

    bool DownloadTarget::dl_bytes(int& result,
                                  dlCtx* dl_ctx,
                                  const char* url,
                                  size_t bytes,
                                  size_t start,
                                  size_t* buffer_len,
                                  int log_level)
    {
#define VARIABLE(x) __dl_bytes_##x##__
        BEGIN(dl_bytes);

        if (start + bytes > *buffer_len)
        {
            VARIABLE(dl) = dl_ctx->dl;

            VARIABLE(fd) = zck_get_fd(zck_dl_get_zck(VARIABLE(dl)));

            if (lseek(VARIABLE(fd), *buffer_len, SEEK_SET) == -1)
            {
                LOG_ERROR << "Seek to download location failed: " << strerror(errno);
                result = 0;
                RETURN(dl_bytes);
            }
            if (*buffer_len >= start + bytes)
            {
                result = 1;
                RETURN(dl_bytes);
            }

            AWAIT(dl_bytes,
                  dl_byte_range(VARIABLE(retval), dl_ctx, url, *buffer_len, (start + bytes) - 1));

            if (VARIABLE(retval) < 1)
            {
                result = VARIABLE(retval);
                RETURN(dl_bytes);
            }

            if (log_level <= ZCK_LOG_DEBUG)
                LOG_INFO << "Downloading " << (unsigned long) start + bytes - *buffer_len
                         << " bytes at position " << (unsigned long) *buffer_len;
            *buffer_len += start + bytes - *buffer_len;
            if (lseek(VARIABLE(fd), start, SEEK_SET) == -1)
            {
                LOG_ERROR << "Seek to byte " << (unsigned long) start
                          << " of temporary file failed: " << strerror(errno);
                result = 0;
                RETURN(dl_bytes);
            }
        }
        result = 1;

        END(dl_bytes);
#undef VARIABLE
    }

    bool DownloadTarget::dl_header(
        int& result, CURL* curl, zckDL* dl, const char* url, int fail_no_ranges, int log_level)
    {
#define VARIABLE(x) __dl_header_##x##__
        BEGIN(dl_header);

        VARIABLE(buffer_len) = 0;
        VARIABLE(start) = 0;

        VARIABLE(dl_ctx) = { 0 };
        VARIABLE(dl_ctx).fail_no_ranges = 1;
        VARIABLE(dl_ctx).dl = dl;
        VARIABLE(dl_ctx).curl = curl;
        VARIABLE(dl_ctx).max_ranges = 1;

        // Download minimum download size and read magic and hash type
        AWAIT(dl_header,
              dl_bytes(VARIABLE(retval),
                       &VARIABLE(dl_ctx),
                       url,
                       zck_get_min_download_size(),
                       VARIABLE(start),
                       &VARIABLE(buffer_len),
                       log_level));
        if (VARIABLE(retval) < 1)
        {
            result = VARIABLE(retval);
            RETURN(dl_header);
        }
        VARIABLE(zck) = zck_dl_get_zck(dl);
        if (VARIABLE(zck) == NULL)
        {
            result = 0;
            RETURN(dl_header);
        }
        if (!zck_read_lead(VARIABLE(zck)))
        {
            result = 0;
            RETURN(dl_header);
        }
        VARIABLE(start) = zck_get_lead_length(VARIABLE(zck));
        AWAIT(dl_header,
              dl_bytes(VARIABLE(retval),
                       &VARIABLE(dl_ctx),
                       url,
                       zck_get_header_length(VARIABLE(zck)) - VARIABLE(start),
                       VARIABLE(start),
                       &VARIABLE(buffer_len),
                       log_level));
        if (!VARIABLE(retval))
        {
            result = 0;
            RETURN(dl_header);
        }
        if (!zck_read_header(VARIABLE(zck)))
        {
            result = 0;
            RETURN(dl_header);
        }
        result = 1;

        END(dl_header);
#undef VARIABLE
    }

    bool DownloadTarget::zchunk_try_open_source()
    {
        int src_fd = open(m_zchunk_source.c_str(), O_RDONLY);
        if (src_fd < 0)
        {
            LOG_INFO << "Unable to open source file " << m_zchunk_source;
            return false;
        }
        m_zck_src = zck_create();
        if (m_zck_src == NULL)
        {
            LOG_ERROR << "Error: " << zck_get_error(NULL);
            zck_clear_error(NULL);
            return false;
        }
        if (!zck_init_read(m_zck_src, src_fd))
        {
            m_zck_src = NULL;
            LOG_ERROR << "Unable to open " << m_zchunk_source << ": " << zck_get_error(m_zck_src);
            return false;
        }
        return true;
    }

    bool DownloadTarget::init_zchunk_target(const std::string& url)
    {
#define VARIABLE(x) __init_zchunk_target_##x##__
        int range_attempt[] = { 255, 127, 7, 2, 1 };

        BEGIN(init_zchunk_target);
        zck_set_log_level(ZCK_LOG_DDEBUG);

        if (!m_zchunk_source.empty())
        {
            if (!zchunk_try_open_source())
            {
                if (fs::exists(m_zchunk_source))
                {
                    // there is a problem with the source file
                    // let's remove it and start fresh next time
                    fs::remove(m_zchunk_source);
                }
            }
        }

        VARIABLE(dst_fd) = open(m_filename.c_str(), O_RDWR | O_CREAT, 0666);
        if (VARIABLE(dst_fd) < 0)
        {
            throw std::runtime_error("Unable to open " + m_filename + ": " + strerror(errno));
        }
        VARIABLE(zck_tgt) = zck_create();
        if (VARIABLE(zck_tgt) == NULL)
        {
            const char* error = zck_get_error(NULL);
            zck_clear_error(NULL);
            throw std::runtime_error(error);
        }
        if (!zck_init_adv_read(VARIABLE(zck_tgt), VARIABLE(dst_fd)))
        {
            throw std::runtime_error(zck_get_error(VARIABLE(zck_tgt)));
        }

        VARIABLE(dl) = zck_dl_init(VARIABLE(zck_tgt));
        if (VARIABLE(dl) == NULL)
        {
            const char* error = zck_get_error(NULL);
            zck_clear_error(NULL);
            throw std::runtime_error(error);
        }

        ;
        AWAIT(init_zchunk_target,
              dl_header(VARIABLE(retval), m_handle, VARIABLE(dl), m_url.c_str(), 1, ZCK_LOG_DEBUG));
        if (!VARIABLE(retval))
        {
            zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
            RETURN(init_zchunk_target);
        }

        // The server doesn't support ranges
        if (VARIABLE(retval) == -1)
        {
            // Download the full file
            lseek(VARIABLE(dst_fd), 0, SEEK_SET);
            if (ftruncate(VARIABLE(dst_fd), 0) < 0)
            {
                perror(NULL);
                zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
            }
            VARIABLE(dl_ctx) = { 0 };
            VARIABLE(dl_ctx).dl = VARIABLE(dl);
            VARIABLE(dl_ctx).curl = m_handle;
            VARIABLE(dl_ctx).max_ranges = 0;
            AWAIT(init_zchunk_target,
                  dl_byte_range(VARIABLE(result), &VARIABLE(dl_ctx), m_url.c_str(), -1, -1));
            if (!VARIABLE(result))
            {
                zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
            }
            lseek(VARIABLE(dst_fd), 0, SEEK_SET);
            if (!zck_read_lead(VARIABLE(zck_tgt)) || !zck_read_header(VARIABLE(zck_tgt)))
            {
                zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
            }
        }
        else
        {
            // If file is already fully downloaded, let's get out of here!
            VARIABLE(retval) = zck_find_valid_chunks(VARIABLE(zck_tgt));
            if (VARIABLE(retval) == 0)
            {
                zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
            }
            if (VARIABLE(retval) == 1)
            {
                LOG_INFO << "Missing chunks: 0";
                LOG_INFO << "Downloaded "
                         << (long unsigned) zck_dl_get_bytes_downloaded(VARIABLE(dl)) << " bytes";
                if (ftruncate(VARIABLE(dst_fd), zck_get_length(VARIABLE(zck_tgt))) < 0)
                {
                    perror(NULL);
                    zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                    RETURN(init_zchunk_target);
                }
                else
                {
                    zchunk_out(0, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                    RETURN(init_zchunk_target);
                }
            }
            if (m_zck_src && !zck_copy_chunks(m_zck_src, VARIABLE(zck_tgt)))
            {
                zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
            }
            zck_reset_failed_chunks(VARIABLE(zck_tgt));
            VARIABLE(dl_ctx) = { 0 };
            VARIABLE(dl_ctx).dl = VARIABLE(dl);
            VARIABLE(dl_ctx).curl = m_handle;
            VARIABLE(dl_ctx).max_ranges = range_attempt[0];
            VARIABLE(dl_ctx).fail_no_ranges = 1;
            VARIABLE(ra_index) = 0;
            m_zchunk_missing = zck_missing_chunks(VARIABLE(zck_tgt));
            LOG_INFO << "Missing chunks: " << m_zchunk_missing;
            while (zck_missing_chunks(VARIABLE(zck_tgt)) > 0)
            {
                VARIABLE(dl_ctx).range_fail = 0;
                zck_dl_reset(VARIABLE(dl));
                VARIABLE(range)
                    = zck_get_missing_range(VARIABLE(zck_tgt), VARIABLE(dl_ctx).max_ranges);
                if (VARIABLE(range) == NULL || !zck_dl_set_range(VARIABLE(dl), VARIABLE(range)))
                {
                    zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                    RETURN(init_zchunk_target);
                }
                while (range_attempt[VARIABLE(ra_index)] > 1
                       && range_attempt[VARIABLE(ra_index) + 1]
                              > zck_get_range_count(VARIABLE(range)))
                {
                    VARIABLE(ra_index)++;
                }
                VARIABLE(range_string) = zck_get_range_char(m_zck_src, VARIABLE(range));
                if (VARIABLE(range_string) == NULL)
                {
                    zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                    RETURN(init_zchunk_target);
                }
                AWAIT(init_zchunk_target,
                      dl_range(VARIABLE(retval),
                               &VARIABLE(dl_ctx),
                               m_url.c_str(),
                               VARIABLE(range_string),
                               1));
                if (VARIABLE(retval) == -1)
                {
                    if (VARIABLE(dl_ctx).max_ranges > 1)
                    {
                        VARIABLE(ra_index) += 1;
                        VARIABLE(dl_ctx).max_ranges = range_attempt[VARIABLE(ra_index)];
                    }
                    LOG_INFO << "Tried downloading too many ranges, reducing to "
                             << VARIABLE(dl_ctx).max_ranges;
                }
                if (!zck_dl_set_range(VARIABLE(dl), NULL))
                {
                    zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                    RETURN(init_zchunk_target);
                }
                zck_range_free(&VARIABLE(range));
                if (!VARIABLE(retval))
                {
                    zchunk_out(1, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                    RETURN(init_zchunk_target);
                }
            }
        }
        LOG_INFO << "Downloaded " << (long unsigned) zck_dl_get_bytes_downloaded(VARIABLE(dl))
                 << " bytes";
        if (ftruncate(VARIABLE(dst_fd), zck_get_length(VARIABLE(zck_tgt))) < 0)
        {
            perror(NULL);
            zchunk_out(10, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
            RETURN(init_zchunk_target);
        }
        switch (zck_validate_data_checksum(VARIABLE(zck_tgt)))
        {
            case -1:
                zchunk_out(1, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
                break;
            case 0:
                zchunk_out(1, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));
                RETURN(init_zchunk_target);
                break;
            default:
                break;
        }

        zchunk_out(0, VARIABLE(dl), VARIABLE(dst_fd), VARIABLE(zck_tgt));

        END(init_zchunk_target);
#undef VARIABLE
    }

    void DownloadTarget::zchunk_out(int exit_val, zckDL* dl, int dst_fd, zckCtx* zck_tgt)
    {
        m_zchunk_err = exit_val;
        if (exit_val > 0)
        {
            if (zck_is_error(NULL))
            {
                LOG_ERROR << zck_get_error(NULL);
                zck_clear_error(NULL);
            }
            if (zck_is_error(m_zck_src))
            {
                LOG_ERROR << zck_get_error(m_zck_src);
            }
            if (zck_is_error(zck_tgt))
            {
                LOG_ERROR << zck_get_error(zck_tgt);
            }
        }
        zck_dl_free(&dl);
        zck_free(&zck_tgt);
        zck_free(&m_zck_src);
    }

    void DownloadTarget::init_curl_target(const std::string& url)
    {
        curl_easy_setopt(m_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_handle, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

        curl_easy_setopt(m_handle, CURLOPT_HEADERFUNCTION, &DownloadTarget::header_callback);
        curl_easy_setopt(m_handle, CURLOPT_HEADERDATA, this);

        curl_easy_setopt(m_handle, CURLOPT_WRITEFUNCTION, &DownloadTarget::write_callback);
        curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, this);

        m_headers = nullptr;
        if (ends_with(url, ".json"))
        {
            curl_easy_setopt(
                m_handle, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, compress, identity");
            m_headers = curl_slist_append(m_headers, "Content-Type: application/json");
        }
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, m_headers);
        curl_easy_setopt(m_handle, CURLOPT_VERBOSE, Context::instance().verbosity >= 2);

        curl_easy_setopt(m_handle, CURLOPT_FOLLOWLOCATION, 1L);

        // DO NOT SET TIMEOUT as it will also take into account multi-start time and
        // it's just wrong curl_easy_setopt(m_handle, CURLOPT_TIMEOUT,
        // Context::instance().read_timeout_secs);

        // TODO while libcurl in conda now _has_ http2 support we need to fix mamba to
        // work properly with it this includes:
        // - setting the cache stuff correctly
        // - fixing how the progress bar works
        curl_easy_setopt(m_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

        // if the request is slower than 30b/s for 60 seconds, cancel.
        curl_easy_setopt(m_handle, CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(m_handle, CURLOPT_LOW_SPEED_LIMIT, 30L);

        curl_easy_setopt(
            m_handle, CURLOPT_CONNECTTIMEOUT, Context::instance().connect_timeout_secs);

        std::string& ssl_verify = Context::instance().ssl_verify;

        if (!ssl_verify.size() && std::getenv("REQUESTS_CA_BUNDLE") != nullptr)
        {
            ssl_verify = std::getenv("REQUESTS_CA_BUNDLE");
        }

        if (ssl_verify.size())
        {
            if (ssl_verify == "<false>")
            {
                curl_easy_setopt(m_handle, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(m_handle, CURLOPT_SSL_VERIFYHOST, 0L);
            }
            else
            {
                if (!fs::exists(ssl_verify))
                {
                    throw std::runtime_error("ssl_verify does not contain a valid file path.");
                }
                else
                {
                    curl_easy_setopt(m_handle, CURLOPT_CAINFO, ssl_verify.c_str());
                }
            }
        }
    }

    bool DownloadTarget::can_retry()
    {
        if (m_is_zchunk)
        {
            return m_retries < size_t(Context::instance().max_retries);
        }
        return m_retries < size_t(Context::instance().max_retries) && http_status >= 500
               && !starts_with(m_url, "file://");
    }

    CURL* DownloadTarget::retry()
    {
        if (m_is_zchunk and (m_zchunk_err == 0))
        {
            return m_handle;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= m_next_retry)
        {
            if (fs::exists(m_filename))
            {
                m_file.close();
                fs::remove(m_filename);
                m_file.open(m_filename);
            }
            if (m_is_zchunk)
            {
                init_zchunk_target(m_url);
            }
            else
            {
                init_curl_target(m_url);
            }
            if (m_has_progress_bar)
            {
                curl_easy_setopt(
                    m_handle, CURLOPT_XFERINFOFUNCTION, &DownloadTarget::progress_callback);
                curl_easy_setopt(m_handle, CURLOPT_XFERINFODATA, this);
            }
            m_retry_wait_seconds = m_retry_wait_seconds * Context::instance().retry_backoff;
            m_next_retry = now + std::chrono::seconds(m_retry_wait_seconds);
            m_retries++;
            return m_handle;
        }
        else
        {
            return nullptr;
        }
    }

    DownloadTarget::~DownloadTarget()
    {
        curl_easy_cleanup(m_handle);
        curl_slist_free_all(m_headers);
    }

    size_t DownloadTarget::write_callback(char* ptr, size_t size, size_t nmemb, void* self)
    {
        auto* s = reinterpret_cast<DownloadTarget*>(self);
        s->m_file.write(ptr, size * nmemb);
        return size * nmemb;
    }

    size_t DownloadTarget::header_callback(char* buffer, size_t size, size_t nitems, void* self)
    {
        auto* s = reinterpret_cast<DownloadTarget*>(self);

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
            // http headers are case insensitive!
            std::string lkey = to_lower(key);
            if (lkey == "etag")
            {
                s->etag = value;
            }
            else if (lkey == "cache-control")
            {
                s->cache_control = value;
            }
            else if (lkey == "last-modified")
            {
                s->mod = value;
            }
        }
        return nitems * size;
    }

    int DownloadTarget::progress_callback(
        void*, curl_off_t total_to_download, curl_off_t now_downloaded, curl_off_t, curl_off_t)
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
            double perc
                = static_cast<double>(now_downloaded) / static_cast<double>(total_to_download);
            std::stringstream postfix;
            postfix << std::setw(6);
            to_human_readable_filesize(postfix, now_downloaded);
            postfix << " / ";
            postfix << std::setw(6);
            to_human_readable_filesize(postfix, total_to_download);
            postfix << " (";
            postfix << std::setw(6);
            to_human_readable_filesize(postfix, get_speed(), 2);
            postfix << "/s)";
            m_progress_bar.set_progress(perc * 100.);
            m_progress_bar.set_postfix(postfix.str());
        }
        if (now_downloaded == 0 && total_to_download != 0)
        {
            std::stringstream postfix;
            to_human_readable_filesize(postfix, total_to_download);
            postfix << " / ?? (";
            to_human_readable_filesize(postfix, get_speed(), 2);
            postfix << "/s)";
            m_progress_bar.set_progress(-1);
            m_progress_bar.set_postfix(postfix.str());
        }
        return 0;
    }

    void DownloadTarget::set_mod_etag_headers(const nlohmann::json& mod_etag)
    {
        auto to_header = [](const std::string& key, const std::string& value) {
            return std::string(key + ": " + value);
        };

        if (mod_etag.find("_etag") != mod_etag.end())
        {
            m_headers = curl_slist_append(m_headers,
                                          to_header("If-None-Match", mod_etag["_etag"]).c_str());
        }
        if (mod_etag.find("_mod") != mod_etag.end())
        {
            m_headers = curl_slist_append(m_headers,
                                          to_header("If-Modified-Since", mod_etag["_mod"]).c_str());
        }
    }

    void DownloadTarget::set_progress_bar(ProgressProxy progress_proxy)
    {
        m_has_progress_bar = true;
        m_progress_bar = progress_proxy;
        curl_easy_setopt(m_handle, CURLOPT_XFERINFOFUNCTION, &DownloadTarget::progress_callback);
        curl_easy_setopt(m_handle, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(m_handle, CURLOPT_NOPROGRESS, 0L);
    }

    void DownloadTarget::set_expected_size(std::size_t size)
    {
        m_expected_size = size;
    }

    const std::string& DownloadTarget::name() const
    {
        return m_name;
    }

    bool DownloadTarget::perform()
    {
        result = curl_easy_perform(m_handle);
        set_result(result);
        return m_finalize_callback ? m_finalize_callback() : true;
    }

    CURL* DownloadTarget::handle()
    {
        return m_handle;
    }

    curl_off_t DownloadTarget::get_speed()
    {
        curl_off_t speed;
        CURLcode res = curl_easy_getinfo(m_handle, CURLINFO_SPEED_DOWNLOAD_T, &speed);
        return res == CURLE_OK ? speed : 0;
    }

    void DownloadTarget::set_result(CURLcode r)
    {
        result = r;
        if (r != CURLE_OK)
        {
            char* effective_url = nullptr;
            curl_easy_getinfo(m_handle, CURLINFO_EFFECTIVE_URL, &effective_url);

            std::stringstream err;
            err << "Download error (" << result << ") " << curl_easy_strerror(result) << " ["
                << effective_url << "]";
            LOG_WARNING << err.str();

            m_next_retry
                = std::chrono::steady_clock::now() + std::chrono::seconds(m_retry_wait_seconds);
            m_progress_bar.set_progress(0);
            m_progress_bar.set_postfix(curl_easy_strerror(result));
            if (m_ignore_failure == false && can_retry() == false)
            {
                throw std::runtime_error(err.str());
            }
        }
    }

    bool DownloadTarget::finalize()
    {
        char* effective_url = nullptr;

        curl_easy_getinfo(m_handle, CURLINFO_RESPONSE_CODE, &http_status);
        curl_easy_getinfo(m_handle, CURLINFO_EFFECTIVE_URL, &effective_url);
        curl_easy_getinfo(m_handle, CURLINFO_SIZE_DOWNLOAD_T, &downloaded_size);

        LOG_INFO << "Transfer finalized, status: " << http_status << " [" << effective_url << "] "
                 << downloaded_size << " bytes";

        if (m_is_zchunk)
        {
            if (init_zchunk_target(m_url))
            {
                std::stringstream msg;
                m_file.close();
                final_url = effective_url;
                msg << "Done";
                m_progress_bar.set_progress(100);
                m_progress_bar.set_postfix(msg.str());
                if (m_finalize_callback)
                {
                    return m_finalize_callback();
                }
                return true;
            }
            else
            {
                return false;
            }
        }

        auto cres = curl_easy_getinfo(m_handle, CURLINFO_SPEED_DOWNLOAD_T, &avg_speed);
        if (cres != CURLE_OK)
        {
            avg_speed = 0;
        }

        if (http_status >= 500 && can_retry())
        {
            // this request didn't work!
            m_next_retry
                = std::chrono::steady_clock::now() + std::chrono::seconds(m_retry_wait_seconds);
            std::stringstream msg;
            msg << "Failed (" << http_status << "), retry in " << m_retry_wait_seconds << "s";
            m_progress_bar.set_progress(0);
            m_progress_bar.set_postfix(msg.str());
            return false;
        }

        m_file.close();

        final_url = effective_url;
        if (m_finalize_callback)
        {
            return m_finalize_callback();
        }
        else
        {
            if (m_has_progress_bar)
            {
                m_progress_bar.mark_as_completed("Downloaded " + m_name);
            }
        }
        return true;
    }

    /**************************************
     * MultiDownloadTarget implementation *
     **************************************/

    MultiDownloadTarget::MultiDownloadTarget()
    {
        m_handle = curl_multi_init();
        curl_multi_setopt(
            m_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, Context::instance().max_parallel_downloads);
    }

    MultiDownloadTarget::~MultiDownloadTarget()
    {
        curl_multi_cleanup(m_handle);
    }

    void MultiDownloadTarget::add(DownloadTarget* target)
    {
        if (!target)
            return;
        CURLMcode code = curl_multi_add_handle(m_handle, target->handle());
        if (code != CURLM_CALL_MULTI_PERFORM)
        {
            if (code != CURLM_OK)
            {
                throw std::runtime_error(curl_multi_strerror(code));
            }
        }
        m_targets.push_back(target);
    }

    bool MultiDownloadTarget::check_msgs(bool failfast)
    {
        int msgs_in_queue;
        CURLMsg* msg;

        while ((msg = curl_multi_info_read(m_handle, &msgs_in_queue)))
        {
            // TODO maybe refactor so that `msg` is passed to current target?
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

            current_target->set_result(msg->data.result);
            if (msg->data.result != CURLE_OK)
            {
                if (current_target->can_retry())
                {
                    curl_multi_remove_handle(m_handle, current_target->handle());
                    m_retry_targets.push_back(current_target);
                    continue;
                }
            }

            if (msg->msg == CURLMSG_DONE)
            {
                LOG_WARNING << "Transfer done ...";
                // We are only interested in messages about finished transfers
                curl_multi_remove_handle(m_handle, current_target->handle());

                // flush file & finalize transfer
                if (!current_target->finalize())
                {
                    // transfer did not work! can we retry?
                    if (current_target->can_retry())
                    {
                        LOG_WARNING << "Adding target to retry!";
                        m_retry_targets.push_back(current_target);
                    }
                    else
                    {
                        if (failfast && current_target->ignore_failure() == false)
                        {
                            throw std::runtime_error("Multi-download failed.");
                        }
                    }
                }
            }
        }
        return true;
    }

    bool MultiDownloadTarget::download(bool failfast)
    {
        LOG_INFO << "Starting to download targets";

        int still_running, repeats = 0;
        const long max_wait_msecs = 1000;
        do
        {
            CURLMcode code = curl_multi_perform(m_handle, &still_running);

            if (code != CURLM_OK)
            {
                throw std::runtime_error(curl_multi_strerror(code));
            }
            check_msgs(failfast);

            if (!m_retry_targets.empty())
            {
                auto it = m_retry_targets.begin();
                while (it != m_retry_targets.end())
                {
                    CURL* curl_handle = (*it)->retry();
                    if (curl_handle != nullptr)
                    {
                        curl_multi_add_handle(m_handle, curl_handle);
                        it = m_retry_targets.erase(it);
                        still_running = 1;
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            long curl_timeout = -1;  // NOLINT(runtime/int)
            code = curl_multi_timeout(m_handle, &curl_timeout);
            if (code != CURLM_OK)
            {
                throw std::runtime_error(curl_multi_strerror(code));
            }

            if (curl_timeout == 0)  // No wait
                continue;

            if (curl_timeout < 0 || curl_timeout > max_wait_msecs)  // Wait no more than 1s
                curl_timeout = max_wait_msecs;

            int numfds;
            code = curl_multi_wait(m_handle, NULL, 0, curl_timeout, &numfds);
            if (code != CURLM_OK)
            {
                throw std::runtime_error(curl_multi_strerror(code));
            }

            if (!numfds)
            {
                repeats++;  // count number of repeated zero numfds
                if (repeats > 1)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            else
            {
                repeats = 0;
            }
        } while ((still_running || !m_retry_targets.empty()) && !is_sig_interrupted());

        if (is_sig_interrupted())
        {
            Console::print("Download interrupted");
            curl_multi_cleanup(m_handle);
            return false;
        }
        return true;
    }
}  // namespace mamba
