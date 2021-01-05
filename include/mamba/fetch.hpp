// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#ifndef MAMBA_FETCH_HPP
#define MAMBA_FETCH_HPP

extern "C"
{
#include <archive.h>
#include <curl/curl.h>
#include <zck.h>
}

#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "output.hpp"
#include "validate.hpp"

// coroutine emulation macros:

#define DECL_STATE(x) int __##x##_state__;
#define RESET_STATE(x) __##x##_state__ = 0;
#define BEGIN(x)                                                                                   \
    switch (__##x##_state__)                                                                       \
    {                                                                                              \
        case 0:
#define YIELD(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        __##x##_state__ = __LINE__;                                                                \
        return false;                                                                              \
        case __LINE__:;                                                                            \
    } while (0)
#define AWAIT(x, f)                                                                                \
    do                                                                                             \
    {                                                                                              \
        case __LINE__:;                                                                            \
            if (!f)                                                                                \
            {                                                                                      \
                __##x##_state__ = __LINE__;                                                        \
                return false;                                                                      \
            }                                                                                      \
    } while (0)
#define RETURN(x)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        __##x##_state__ = 0;                                                                       \
        return true;                                                                               \
    } while (0)
#define END(x)                                                                                     \
    }                                                                                              \
    __##x##_state__ = 0;                                                                           \
    return true

namespace mamba
{
    class DownloadTarget;

    typedef struct dlCtx
    {
        CURL* curl;
        zckDL* dl;
        int fail_no_ranges;
        int range_fail;
        int max_ranges;
        DownloadTarget* target;
    } dlCtx;

    class DownloadTarget
    {
    public:
        DownloadTarget() = default;
        DownloadTarget(const std::string& name,
                       const std::string& url,
                       const std::string& filename,
                       const std::string& zchunk_source = "");
        ~DownloadTarget();

        static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* self);
        static size_t header_callback(char* buffer, size_t size, size_t nitems, void* self);

        int progress_callback(
            void*, curl_off_t total_to_download, curl_off_t now_downloaded, curl_off_t, curl_off_t);
        void set_mod_etag_headers(const nlohmann::json& mod_etag);
        void set_progress_bar(ProgressProxy progress_proxy);
        void set_expected_size(std::size_t size);

        const std::string& name() const;

        void init_curl_target(const std::string& url);
        bool perform();
        CURL* handle();

        curl_off_t get_speed();

        template <class C>
        inline void set_finalize_callback(bool (C::*cb)(), C* data)
        {
            m_finalize_callback = std::bind(cb, data);
        }

        inline void set_ignore_failure(bool yes)
        {
            m_ignore_failure = yes;
        }

        inline bool ignore_failure() const
        {
            return m_ignore_failure;
        }

        void set_result(CURLcode r);
        bool finalize();

        bool can_retry();
        CURL* retry();

        CURLcode result;
        bool failed = false;
        int http_status = 10000;
        curl_off_t downloaded_size = 0;
        curl_off_t avg_speed = 0;
        std::string final_url;

        std::string etag, mod, cache_control;

        bool is_zchunk();
        bool init_zchunk_target(const std::string& url);
        void zchunk_out(int exit_val, zckDL* dl, int dst_fd, zckCtx* zck_tgt);
        bool zchunk_try_open_source();
        bool dl_range(int& result, dlCtx* dl_ctx, const char* url, char* range, int is_chunk);
        bool dl_byte_range(int& result, dlCtx* dl_ctx, const char* url, int start, int end);
        bool dl_bytes(int& result,
                      dlCtx* dl_ctx,
                      const char* url,
                      size_t bytes,
                      size_t start,
                      size_t* buffer_len,
                      int log_level);
        bool dl_header(
            int& result, CURL* curl, zckDL* dl, const char* url, int fail_no_ranges, int log_level);

    private:
        std::function<bool()> m_finalize_callback;

        std::string m_name, m_filename, m_url, m_zchunk_source;
        zckCtx* m_zck_src;

        // validation
        std::size_t m_expected_size = 0;

        std::chrono::steady_clock::time_point m_progress_throttle_time;

        // retry & backoff
        std::chrono::steady_clock::time_point m_next_retry;
        std::size_t m_retry_wait_seconds = Context::instance().retry_timeout;
        std::size_t m_retries = 0;

        CURL* m_handle;
        curl_slist* m_headers;

        bool m_has_progress_bar = false;
        bool m_ignore_failure = false;
        bool m_is_zchunk;
        int m_zchunk_err;
        int m_zchunk_missing;

        ProgressProxy m_progress_bar;

        std::ofstream m_file;

        // coroutine emulation variables that must be preserved between calls
        // have to be used with VARIABLE(x)

        DECL_STATE(dl_range);
#define VARIABLE(x) __dl_range_##x##__
        CURL* VARIABLE(curl);
#undef VARIABLE

        DECL_STATE(dl_byte_range);
#define VARIABLE(x) __dl_byte_range_##x##__
        char* VARIABLE(range);
#undef VARIABLE

        DECL_STATE(dl_bytes);
#define VARIABLE(x) __dl_bytes_##x##__
        zckDL* VARIABLE(dl);
        int VARIABLE(fd);
        int VARIABLE(retval);
#undef VARIABLE

        DECL_STATE(dl_header);
#define VARIABLE(x) __dl_header_##x##__
        size_t VARIABLE(buffer_len);
        size_t VARIABLE(start);
        dlCtx VARIABLE(dl_ctx);
        zckCtx* VARIABLE(zck);
        int VARIABLE(retval);
#undef VARIABLE

        DECL_STATE(init_zchunk_target);
#define VARIABLE(x) __init_zchunk_target_##x##__
        int VARIABLE(result);
        int VARIABLE(retval);
        zckDL* VARIABLE(dl);
        zckCtx* VARIABLE(zck_tgt);
        int VARIABLE(dst_fd);
        char* VARIABLE(range_string);
        zckRange* VARIABLE(range);
        int VARIABLE(ra_index);
        dlCtx VARIABLE(dl_ctx);
#undef VARIABLE
    };

    class MultiDownloadTarget
    {
    public:
        MultiDownloadTarget();
        ~MultiDownloadTarget();

        void add(DownloadTarget* target);
        bool check_msgs(bool failfast);
        bool download(bool failfast);

    private:
        std::vector<DownloadTarget*> m_targets;
        std::vector<DownloadTarget*> m_retry_targets;
        CURLM* m_handle;
    };

}  // namespace mamba

#endif  // MAMBA_FETCH_HPP
