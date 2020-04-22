#include <csignal>

// TODO: remove minilog
#include "minilog.hpp"
#include "util.hpp"
#include "context.hpp"

namespace mamba
{
    Context::Context()
    {
        set_verbosity(0);
        on_ci = (std::getenv("CI") != nullptr);
        if (on_ci)
        {
            no_progress_bars = true;
        }

        std::signal(SIGINT, [](int signum) {
            instance().sig_interrupt = true;
        });
    }

    Context& Context::instance()
    {
        static Context ctx;
        return ctx;
    }

    void Context::set_verbosity(int lvl)
    {
        if (lvl == 0)
        {
            minilog::global_log_severity = 3;
        }
        else if (lvl == 1)
        {
            minilog::global_log_severity = 1;
        }
        else 
        {
            minilog::global_log_severity = 0;
        }

        this->verbosity = lvl;
    }

    const char* Context::proxy_match(const std::string& url) const
    {
        if (proxies.size() == 0)
        {
            return nullptr;
        }

        std::size_t max_match = 0;
        const char* match;
        for (auto& p : proxies)
        {
            if (starts_with(url, p.first))
            {
                auto match_size = p.first.size();
                if (match_size > max_match)
                {
                    match = p.second.c_str();
                    max_match = match_size;
                }
            }
        }
        if (max_match != 0)
        {
            return match;
        }
        return nullptr;
    }

}
