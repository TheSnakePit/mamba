// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#ifndef MAMBA_CONTEXT_HPP
#define MAMBA_CONTEXT_HPP

#include <map>
#include <string>
#include <vector>

#include "mamba_fs.hpp"

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
        bool is_micromamba = false;

        fs::path target_prefix = std::getenv("CONDA_PREFIX") ? std::getenv("CONDA_PREFIX") : "";
        // Need to prevent circular imports here (otherwise using env::get())
        fs::path root_prefix
            = std::getenv("MAMBA_ROOT_PREFIX") ? std::getenv("MAMBA_ROOT_PREFIX") : "";
        fs::path conda_prefix = root_prefix;

        // TODO check writable and add other potential dirs
        std::vector<fs::path> envs_dirs = { root_prefix / "envs" };
        std::vector<fs::path> pkgs_dirs = { root_prefix / "pkgs" };

        bool use_index_cache = false;
        std::size_t local_repodata_ttl = 1;  // take from header
        bool offline = false;
        bool quiet = false;
        bool json = false;
        bool strict_channel_priority = false;
        bool auto_activate_base = false;

        long max_parallel_downloads = 5;
        int verbosity = 0;

        bool dev = false;
        bool on_ci = false;
        bool no_progress_bars = false;
        bool dry_run = false;
        bool use_zchunk = false;
        bool always_yes = false;

        // debug helpers
        bool keep_temp_files = false;
        bool keep_temp_directories = false;

        bool change_ps1 = true;

        int connect_timeout_secs = 10;
        // int read_timeout_secs = 60;
        int retry_timeout = 2;  // seconds
        int retry_backoff = 3;  // retry_timeout * retry_backoff
        int max_retries = 3;    // max number of retries

        std::string env_prompt = "({default_env}) ";

        // ssl verify can be either an empty string (regular SSL verification),
        // the string "<false>" to indicate no SSL verification, or a path to
        // a directory with cert files, or a cert file.
        std::string ssl_verify = "";

        // Conda compat
        bool add_pip_as_python_dependency = true;

        void set_verbosity(int lvl);

        static std::string platform();
        static std::vector<std::string> platforms();

        std::vector<std::string> channels = {};

        static Context& instance();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

    private:
        Context();
        ~Context() = default;
    };
}  // namespace mamba

#endif  // MAMBA_CONTEXT_HPP
