#ifndef MAMBA_CONTEXT_HPP
#define MAMBA_CONTEXT_HPP

#include <vector>
#include <string>

#include "thirdparty/filesystem.hpp"

namespace fs = ghc::filesystem;

#define ROOT_ENV_NAME "base"

namespace mamba
{
    std::string env_name(const fs::path& prefix);
    fs::path locate_prefix_by_name(const std::string& name);

    // Context singleton class
    class Context
    {
    public:

        std::string conda_version = "3.8.0";
        std::string current_command = "mamba";

        fs::path target_prefix = std::getenv("CONDA_PREFIX") ? std::getenv("CONDA_PREFIX") : "";;
        // Need to prevent circular imports here (otherwise using env::get())
        fs::path root_prefix = std::getenv("MAMBA_ROOT_PREFIX") ? std::getenv("MAMBA_ROOT_PREFIX") : "";
        fs::path conda_prefix = root_prefix;

        // TODO check writable and add other potential dirs
        std::vector<fs::path> envs_dirs = { root_prefix / "envs" };

        bool use_index_cache = false;
        std::size_t local_repodata_ttl = 1; // take from header
        bool offline = false;
        bool quiet = false;
        bool json = false;
        bool auto_activate_base = false;

        long max_parallel_downloads = 5;
        int verbosity = 0;

        bool dev = false;
        bool on_ci = false;
        bool no_progress_bars = false;
        bool dry_run = false;
        bool always_yes = false;

        // debug helpers
        bool keep_temp_files = false;
        bool keep_temp_directories = false;

        bool sig_interrupt = false;

        bool change_ps1 = true;

        int retry_timeout = 2; // seconds
        int retry_backoff = 3; // retry_timeout * retry_backoff
        int max_retries = 3;  // max number of retries

        std::string env_prompt = "({default_env}) ";

        // ssl verify can be either an empty string (regular SSL verification),
        // the string "<false>" to indicate no SSL verification, or a path to 
        // a directory with cert files, or a cert file.
        std::string ssl_verify = "";

        void set_verbosity(int lvl);
    
        static Context& instance();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

    private:

        Context();
        ~Context() = default;
    };
}

#endif // MAMBA_CONTEXT_HPP
