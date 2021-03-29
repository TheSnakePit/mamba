// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include "umamba.hpp"
#include "info.hpp"
#include "common_options.hpp"

#include "mamba/configuration.hpp"
#include "mamba/context.hpp"
#include "mamba/output.hpp"
#include "mamba/thread_utils.hpp"
#include "mamba/info.hpp"

#ifdef VENDORED_CLI11
#include "mamba/CLI.hpp"
#else
#include <CLI/CLI.hpp>
#endif

using namespace mamba;  // NOLINT(build/namespaces)


/* TODO: Clean up, NOT USED
void
update_specs(std::vector<std::string>& specs)
{
    if (update_options.update_all)
    {
        auto& ctx = Context::instance();
        if (ctx.target_prefix.empty())
        {
            throw std::runtime_error(
                "No active target prefix.\n\nRun $ micromamba activate <PATH_TO_MY_ENV>\nto activate
an environment.\n");
        }

        PrefixData prefix_data(ctx.target_prefix);
        prefix_data.load();
        prefix_data.add_virtual_packages(get_virtual_packages());

        for (const auto& package : prefix_data.m_package_records)
        {
            auto& name = package.second.name;
            if (name != "python")
            {
                specs.push_back(name);
            }
        }
    }

    install_specs(specs, false, SOLVER_UPDATE);
}
*/

int
main(int argc, char** argv)
{
    auto& ctx = Context::instance();

    ctx.is_micromamba = true;
    ctx.custom_banner = umamba_banner;

    CLI::App app{ "Version: " + mamba::version() + "\n" };
    set_umamba_command(&app);

    try
    {
        CLI11_PARSE(app, argc, argv);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << e.what();
        set_sig_interrupted();
        return 1;
    }

    if (app.get_subcommands().size() == 0)
    {
        auto& config = Configuration::instance();
        config.load(MAMBA_ALLOW_ROOT_PREFIX | MAMBA_ALLOW_FALLBACK_PREFIX
                    | MAMBA_ALLOW_EXISTING_PREFIX | MAMBA_ALLOW_MISSING_PREFIX);
        Console::print(app.help());
    }
    if (app.got_subcommand("config") && app.get_subcommand("config")->get_subcommands().size() == 0)
    {
        auto& config = Configuration::instance();
        config.load(MAMBA_ALLOW_ROOT_PREFIX | MAMBA_ALLOW_FALLBACK_PREFIX
                    | MAMBA_ALLOW_EXISTING_PREFIX | MAMBA_ALLOW_MISSING_PREFIX);
        Console::print(app.get_subcommand("config")->help());
    }

    return 0;
}
