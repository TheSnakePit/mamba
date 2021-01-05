# Copyright (C) 2019, QuantStack
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import absolute_import, division, print_function, unicode_literals

import codecs
import os
import sys
from logging import getLogger
from os.path import isdir, isfile, join

# create support
from conda.base.constants import DepsModifier, UpdateModifier
from conda.base.context import context
from conda.cli import common as cli_common
from conda.cli.common import (
    check_non_admin,
    confirm_yn,
    ensure_name_or_prefix,
    specs_from_url,
)
from conda.cli.install import check_prefix, clone, get_revision, print_activate
from conda.cli.main import generate_parser, init_loggers
from conda.common.compat import on_win
from conda.common.path import paths_equal
from conda.core.envs_manager import unregister_env
from conda.core.link import PrefixSetup, UnlinkLinkTransaction
from conda.core.package_cache_data import PackageCacheData
from conda.core.prefix_data import PrefixData
from conda.core.solve import get_pinned_specs
from conda.exceptions import (
    CondaEnvironmentError,
    CondaExitZero,
    CondaOSError,
    CondaSystemExit,
    CondaValueError,
    DirectoryNotACondaEnvironmentError,
    DryRunExit,
    EnvironmentLocationNotFound,
    NoBaseEnvironmentError,
    PackageNotInstalledError,
    PackagesNotFoundError,
    TooManyArgumentsError,
)
from conda.gateways.disk.create import mkdir_p
from conda.gateways.disk.delete import delete_trash, path_is_clean, rm_rf
from conda.gateways.disk.test import is_conda_environment
from conda.misc import explicit, touch_nonadmin
from conda.models.match_spec import MatchSpec

import mamba
import mamba.mamba_api as api
from mamba import repoquery as repoquery_api
from mamba.utils import get_installed_jsonfile, init_api_context, load_channels, to_txn

if sys.version_info < (3, 2):
    sys.stdout = codecs.lookup("utf-8")[-1](sys.stdout)
elif sys.version_info < (3, 7):
    sys.stdout = codecs.getwriter("utf-8")(sys.stdout.detach())
else:
    sys.stdout.reconfigure(encoding="utf-8")


log = getLogger(__name__)
stderrlog = getLogger("conda.stderr")

banner = f"""
                  __    __    __    __
                 /  \\  /  \\  /  \\  /  \\
                /    \\/    \\/    \\/    \\
███████████████/  /██/  /██/  /██/  /████████████████████████
              /  / \\   / \\   / \\   / \\  \\____
             /  /   \\_/   \\_/   \\_/   \\    o \\__,
            / _/                       \\_____/  `
            |/
        ███╗   ███╗ █████╗ ███╗   ███╗██████╗  █████╗
        ████╗ ████║██╔══██╗████╗ ████║██╔══██╗██╔══██╗
        ██╔████╔██║███████║██╔████╔██║██████╔╝███████║
        ██║╚██╔╝██║██╔══██║██║╚██╔╝██║██╔══██╗██╔══██║
        ██║ ╚═╝ ██║██║  ██║██║ ╚═╝ ██║██████╔╝██║  ██║
        ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝╚═════╝ ╚═╝  ╚═╝

        mamba ({mamba.__version__}) supported by @QuantStack

        GitHub:  https://github.com/mamba-org/mamba
        Twitter: https://twitter.com/QuantStack

█████████████████████████████████████████████████████████████
"""


class MambaException(Exception):
    pass


solver_options = [(api.SOLVER_FLAG_ALLOW_DOWNGRADE, 1)]


def specs_from_args(args, json=False):
    def arg2spec(arg, json=False, update=False):
        try:
            spec = MatchSpec(arg)
        except Exception:
            raise CondaValueError("invalid package specification: %s" % arg)

        name = spec.name
        if not spec._is_simple() and update:
            raise CondaValueError(
                """version specifications not allowed with 'update'; use
        conda update  %s%s  or
        conda install %s"""
                % (name, " " * (len(arg) - len(name)), arg)
            )

        return spec

    return [arg2spec(arg, json=json) for arg in args]


use_mamba_experimental = False
use_zchunk = False


def handle_txn(unlink_link_transaction, prefix, args, newenv, remove_op=False):
    if unlink_link_transaction.nothing_to_do:
        if remove_op:
            # No packages found to remove from environment
            raise PackagesNotFoundError(args.package_names)
        elif not newenv:
            if context.json:
                cli_common.stdout_json_success(
                    message="All requested packages already installed."
                )
            return

    if context.dry_run:
        actions = unlink_link_transaction._make_legacy_action_groups()[0]
        if context.json:
            cli_common.stdout_json_success(prefix=prefix, actions=actions, dry_run=True)
        raise DryRunExit()

    try:
        unlink_link_transaction.download_and_extract()
        if context.download_only:
            raise CondaExitZero(
                "Package caches prepared. UnlinkLinkTransaction cancelled with "
                "--download-only option."
            )
        unlink_link_transaction.execute()

    except SystemExit as e:
        raise CondaSystemExit("Exiting", e)

    if newenv:
        touch_nonadmin(prefix)
        print_activate(args.name if args.name else prefix)

    if context.json:
        actions = unlink_link_transaction._make_legacy_action_groups()[0]
        cli_common.stdout_json_success(prefix=prefix, actions=actions)


def remove(args, parser):
    if not (args.all or args.package_names):
        raise CondaValueError(
            "no package names supplied,\n"
            '       try "mamba remove -h" for more details'
        )

    prefix = context.target_prefix
    check_non_admin()
    init_api_context()

    if args.all and prefix == context.default_prefix:
        raise CondaEnvironmentError(
            "cannot remove current environment. \
                                     deactivate and run mamba remove again"
        )

    if args.all and path_is_clean(prefix):
        # full environment removal was requested, but environment doesn't exist anyway
        return 0

    if args.all:
        if prefix == context.root_prefix:
            raise CondaEnvironmentError(
                "cannot remove root environment,\n"
                "       add -n NAME or -p PREFIX option"
            )
        print("\nRemove all packages in environment %s:\n" % prefix, file=sys.stderr)

        if "package_names" in args:
            stp = PrefixSetup(
                target_prefix=prefix,
                unlink_precs=tuple(PrefixData(prefix).iter_records()),
                link_precs=(),
                remove_specs=(),
                update_specs=(),
                neutered_specs=(),
            )
            txn = UnlinkLinkTransaction(stp)
            try:
                handle_txn(txn, prefix, args, False, True)
            except PackagesNotFoundError:
                print(
                    "No packages found in %s. Continuing environment removal" % prefix
                )

        rm_rf(prefix, clean_empty_parents=True)
        unregister_env(prefix)

        return

    else:
        if args.features:
            specs = tuple(MatchSpec(track_features=f) for f in set(args.package_names))
        else:
            specs = [s for s in specs_from_args(args.package_names)]
        if not context.quiet:
            print("Removing specs: {}".format([s.conda_build_form() for s in specs]))

        installed_json_f, installed_pkg_recs = get_installed_jsonfile(prefix)

        mamba_solve_specs = [s.conda_build_form() for s in specs]

        solver_options.append((api.SOLVER_FLAG_ALLOW_UNINSTALL, 1))

        pool = api.Pool()
        repos = []

        # add installed
        if use_mamba_experimental:
            prefix_data = api.PrefixData(context.target_prefix)
            prefix_data.load()
            repo = api.Repo(pool, prefix_data)
            repos.append(repo)
        else:
            repo = api.Repo(pool, "installed", installed_json_f.name, "")
            repo.set_installed()
            repos.append(repo)

        solver = api.Solver(pool, solver_options)
        solver.add_jobs(mamba_solve_specs, api.SOLVER_ERASE)
        success = solver.solve()
        if not success:
            print(solver.problems_to_str())
            exit_code = 1
            return exit_code

        package_cache = api.MultiPackageCache(context.pkgs_dirs)
        transaction = api.Transaction(solver, package_cache)
        downloaded = transaction.prompt(
            PackageCacheData.first_writable().pkgs_dir, repos
        )
        if not downloaded:
            exit(0)

        mmb_specs, to_link, to_unlink = transaction.to_conda()
        transaction.log_json()

        specs_to_add = [MatchSpec(m) for m in mmb_specs[0]]
        specs_to_remove = [MatchSpec(m) for m in mmb_specs[1]]

        conda_transaction = to_txn(
            specs_to_add,
            specs_to_remove,
            prefix,
            to_link,
            to_unlink,
            installed_pkg_recs,
        )
        handle_txn(conda_transaction, prefix, args, False, True)


def install(args, parser, command="install"):
    """
    mamba install, mamba update, and mamba create
    """
    context.validate_configuration()
    check_non_admin()

    init_api_context(use_mamba_experimental, use_zchunk)

    newenv = bool(command == "create")
    isinstall = bool(command == "install")
    solver_task = api.SOLVER_INSTALL

    isupdate = bool(command == "update")
    if isupdate:
        solver_task = api.SOLVER_UPDATE
        solver_options.clear()

    if newenv:
        ensure_name_or_prefix(args, command)
    prefix = context.target_prefix
    if newenv:
        check_prefix(prefix, json=context.json)
    if context.force_32bit and prefix == context.root_prefix:
        raise CondaValueError("cannot use CONDA_FORCE_32BIT=1 in base env")
    if isupdate and not (
        args.file
        or args.packages
        or context.update_modifier == UpdateModifier.UPDATE_ALL
    ):
        raise CondaValueError(
            """no package names supplied
# If you want to update to a newer version of Anaconda, type:
#
# $ conda update --prefix %s anaconda
"""
            % prefix
        )

    if not newenv:
        if isdir(prefix):
            if on_win:
                delete_trash(prefix)

            if not isfile(join(prefix, "conda-meta", "history")):
                if paths_equal(prefix, context.conda_prefix):
                    raise NoBaseEnvironmentError()
                else:
                    if not path_is_clean(prefix):
                        raise DirectoryNotACondaEnvironmentError(prefix)
            else:
                # fall-through expected under normal operation
                pass
        else:
            if hasattr(args, "mkdir") and args.mkdir:
                try:
                    mkdir_p(prefix)
                except EnvironmentError as e:
                    raise CondaOSError(
                        "Could not create directory: %s" % prefix, caused_by=e
                    )
            else:
                raise EnvironmentLocationNotFound(prefix)

    prefix = context.target_prefix

    #############################
    # Get SPECS                 #
    #############################

    args_packages = [s.strip("\"'") for s in args.packages]
    if newenv and not args.no_default_packages:
        # Override defaults if they are specified at the command line
        # TODO: rework in 4.4 branch using MatchSpec
        args_packages_names = [
            pkg.replace(" ", "=").split("=", 1)[0] for pkg in args_packages
        ]
        for default_pkg in context.create_default_packages:
            default_pkg_name = default_pkg.replace(" ", "=").split("=", 1)[0]
            if default_pkg_name not in args_packages_names:
                args_packages.append(default_pkg)

    num_cp = sum(s.endswith(".tar.bz2") for s in args_packages)
    if num_cp:
        if num_cp == len(args_packages):
            explicit(args_packages, prefix, verbose=not (context.quiet or context.json))
            return
        else:
            raise CondaValueError(
                "cannot mix specifications with conda package" " filenames"
            )

    specs = []

    index_args = {
        "use_cache": args.use_index_cache,
        "channel_urls": context.channels,
        "unknown": args.unknown,
        "prepend": not args.override_channels,
        "use_local": args.use_local,
    }

    if args.file:
        file_specs = []
        for fpath in args.file:
            try:
                file_specs += specs_from_url(fpath, json=context.json)
            except UnicodeError:
                raise CondaValueError(
                    "Error reading file, file should be a text file containing"
                    " packages \nconda create --help for details"
                )
        if "@EXPLICIT" in file_specs:
            explicit(
                file_specs,
                prefix,
                verbose=not (context.quiet or context.json),
                index_args=index_args,
            )
            return
        specs.extend([MatchSpec(s) for s in file_specs])

    specs.extend(specs_from_args(args_packages, json=context.json))

    # update channels from package specs (e.g. mychannel::mypackage adds mychannel)
    channels = [c for c in context.channels]
    for spec in specs:
        # CONDA TODO: correct handling for subdir isn't yet done
        spec_channel = spec.get_exact_value("channel")
        if spec_channel and spec_channel not in channels:
            channels.append(spec_channel)

    index_args["channel_urls"] = channels

    installed_json_f, installed_pkg_recs = get_installed_jsonfile(prefix)

    if isinstall and args.revision:
        get_revision(args.revision, json=context.json)
    elif isinstall and not (args.file or args_packages):
        raise CondaValueError(
            "too few arguments, " "must supply command line package specs or --file"
        )

    installed_names = [i_rec.name for i_rec in installed_pkg_recs]
    # for 'conda update', make sure the requested specs actually exist in the prefix
    # and that they are name-only specs
    if isupdate and context.update_modifier == UpdateModifier.UPDATE_ALL:
        for i in installed_names:
            if i != "python":
                specs.append(MatchSpec(i))

        prefix_data = PrefixData(prefix)
        for s in args_packages:
            s = MatchSpec(s)
            if s.name == "python":
                specs.append(s)
            if not s.is_name_only_spec:
                raise CondaValueError(
                    "Invalid spec for 'conda update': %s\n"
                    "Use 'conda install' instead." % s
                )
            if not prefix_data.get(s.name, None):
                raise PackageNotInstalledError(prefix, s.name)

    elif context.update_modifier == UpdateModifier.UPDATE_DEPS:
        # find the deps for each package and add to the update job
        # solver_task |= api.SOLVER_FORCEBEST
        final_specs = specs
        for spec in specs:
            prec = installed_pkg_recs[installed_names.index(spec.name)]
            for dep in prec.depends:
                ms = MatchSpec(dep)
                if ms.name != "python":
                    final_specs.append(MatchSpec(ms.name))
        specs = set(final_specs)

    if newenv and args.clone:
        if args.packages:
            raise TooManyArgumentsError(
                0,
                len(args.packages),
                list(args.packages),
                "did not expect any arguments for --clone",
            )

        clone(
            args.clone,
            prefix,
            json=context.json,
            quiet=(context.quiet or context.json),
            index_args=index_args,
        )
        touch_nonadmin(prefix)
        print_activate(args.name if args.name else prefix)
        return

    if not (context.quiet or context.json):
        print("\nLooking for: {}\n".format([str(s) for s in specs]))

    spec_names = [s.name for s in specs]

    # If python was not specified, check if it is installed.
    # If yes, add the installed python to the specs to prevent updating it.
    python_constraint = None

    if "python" not in spec_names:
        if "python" in installed_names:
            i = installed_names.index("python")
            version = installed_pkg_recs[i].version
            python_constraint = MatchSpec("python==" + version).conda_build_form()

    mamba_solve_specs = [s.__str__() for s in specs]

    pool = api.Pool()

    repos = []

    if use_mamba_experimental or context.force_reinstall:
        prefix_data = api.PrefixData(context.target_prefix)
        prefix_data.load()

    # add installed
    if use_mamba_experimental:
        repo = api.Repo(pool, prefix_data)
        repos.append(repo)
    else:
        repo = api.Repo(pool, "installed", installed_json_f.name, "")
        repo.set_installed()
        repos.append(repo)

    if newenv and not specs:
        # creating an empty environment with e.g. "mamba create -n my_env"
        # should not download the repodata
        index = []
        specs_to_add = []
        specs_to_remove = []
        to_link = []
        to_unlink = []
        installed_pkg_recs = []
    else:
        index = load_channels(pool, channels, repos)

        if context.force_reinstall:
            solver = api.Solver(pool, solver_options, prefix_data)
        else:
            solver = api.Solver(pool, solver_options)

        solver.set_postsolve_flags(
            [
                (api.MAMBA_NO_DEPS, context.deps_modifier == DepsModifier.NO_DEPS),
                (api.MAMBA_ONLY_DEPS, context.deps_modifier == DepsModifier.ONLY_DEPS),
                (api.MAMBA_FORCE_REINSTALL, context.force_reinstall),
            ]
        )
        solver.add_jobs(mamba_solve_specs, solver_task)

        if not context.force_reinstall:
            # as a security feature this will _always_ attempt to upgradecertain
            # packages
            for a_pkg in [_.name for _ in context.aggressive_update_packages]:
                if a_pkg in installed_names:
                    solver.add_jobs([a_pkg], api.SOLVER_UPDATE)

        if python_constraint:
            solver.add_pin(python_constraint)

        pinned_specs = get_pinned_specs(context.target_prefix)
        if pinned_specs:
            conda_prefix_data = PrefixData(context.target_prefix)
        for s in pinned_specs:
            x = conda_prefix_data.query(s.name)
            if x:
                for el in x:
                    if not s.match(el):
                        print(
                            "Your pinning does not match what's currently installed."
                            " Please remove the pin and fix your installation"
                        )
                        print("  Pin: {}".format(s))
                        print("  Currently installed: {}".format(el))
                        exit(1)
            solver.add_pin(str(s))

        success = solver.solve()
        if not success:
            print(solver.problems_to_str())
            exit_code = 1
            return exit_code

        package_cache = api.MultiPackageCache(context.pkgs_dirs)
        transaction = api.Transaction(solver, package_cache)
        mmb_specs, to_link, to_unlink = transaction.to_conda()

        specs_to_add = [MatchSpec(m) for m in mmb_specs[0]]
        specs_to_remove = [MatchSpec(m) for m in mmb_specs[1]]

        transaction.log_json()

        downloaded = transaction.prompt(
            PackageCacheData.first_writable().pkgs_dir, repos
        )
        if not downloaded:
            exit(0)
        PackageCacheData.first_writable().reload()

    # if use_mamba_experimental and not os.name == "nt":
    if use_mamba_experimental:
        if newenv and not isdir(context.target_prefix) and not context.dry_run:
            mkdir_p(prefix)

        transaction.execute(prefix_data, PackageCacheData.first_writable().pkgs_dir)
    else:
        conda_transaction = to_txn(
            specs_to_add,
            specs_to_remove,
            prefix,
            to_link,
            to_unlink,
            installed_pkg_recs,
            index,
        )
        handle_txn(conda_transaction, prefix, args, newenv)

    try:
        installed_json_f.close()
        os.unlink(installed_json_f.name)
    except Exception:
        pass


def create(args, parser):
    if is_conda_environment(context.target_prefix):
        if paths_equal(context.target_prefix, context.root_prefix):
            raise CondaValueError("The target prefix is the base prefix. Aborting.")
        confirm_yn(
            "WARNING: A conda environment already exists at '%s'\n"
            "Remove existing environment" % context.target_prefix,
            default="no",
            dry_run=False,
        )
        log.info("Removing existing environment %s", context.target_prefix)
        rm_rf(context.target_prefix)
    elif isdir(context.target_prefix):
        confirm_yn(
            "WARNING: A directory already exists at the target location '%s'\n"
            "but it is not a conda environment.\n"
            "Continue creating environment" % context.target_prefix,
            default="no",
            dry_run=False,
        )
    return install(args, parser, "create")


def update(args, parser):
    if context.force:
        print(
            "\n\n"
            "WARNING: The --force flag will be removed in a future conda release.\n"
            "         See 'conda update --help'"
            " for details about the --force-reinstall\n"
            "         and --clobber flags.\n"
            "\n",
            file=sys.stderr,
        )

    # need to implement some modifications on the update function
    return install(args, parser, "update")


def repoquery(args, parser):
    if not args.subcmd:
        print("repoquery needs a subcommand (search, depends or whoneeds)")
        print("eg:")
        print("    $ mamba repoquery search xtensor\n")
        exit(1)

    # print(args)
    if args.platform:
        platform = args.platform
    else:
        platform = context.subdir

    channels = None
    if hasattr(args, "channel"):
        channels = args.channel
    if args.all_channels or (channels is None and args.subcmd == "search"):
        if channels:
            print("WARNING: Using all channels instead of configured channels")
        channels = context.channels

    use_installed = args.installed
    if args.no_installed:
        use_installed = False

    # if we're asking for depends and channels are given, disregard
    # installed packages to prevent weird mixing
    if args.subcmd in ("depends", "whoneeds") and use_installed and channels:
        use_installed = False

    only_installed = True
    if args.subcmd == "search" and not args.installed:
        only_installed = False
    elif args.all_channels or (channels and len(channels)):
        only_installed = False

    if only_installed and args.no_installed:
        print("No channels selected.")
        print("Activate -a to search all channels.")
        exit(1)

    if not context.json:
        print("\nExecuting the query %s\n" % args.package_query)

    if context.json:
        fmt = api.QueryFormat.JSON
    elif hasattr(args, "tree") and args.tree:
        fmt = api.QueryFormat.TREE
    else:
        fmt = api.QueryFormat.TABLE

    pool = repoquery_api.create_pool(channels, platform, use_installed)

    print("\n")
    print(repoquery_api._repoquery(args.subcmd, args.package_query, pool, fmt))


def do_call(args, parser):
    relative_mod, func_name = args.func.rsplit(".", 1)

    # func_name should always be 'execute'
    if relative_mod in [
        ".main_list",
        ".main_search",
        ".main_run",
        ".main_clean",
        ".main_info",
    ]:
        from importlib import import_module

        module = import_module("conda.cli" + relative_mod, __name__.rsplit(".", 1)[0])
        exit_code = getattr(module, func_name)(args, parser)
    elif relative_mod == ".main_install":
        exit_code = install(args, parser, "install")
    elif relative_mod == ".main_remove":
        exit_code = remove(args, parser)
    elif relative_mod == ".main_create":
        exit_code = create(args, parser)
    elif relative_mod == ".main_update":
        exit_code = update(args, parser)
    elif relative_mod == ".main_repoquery":
        exit_code = repoquery(args, parser)
    else:
        print(
            "Currently, only install, create, list, search, run,"
            " info and clean are supported through mamba."
        )

        return 0
    return exit_code


def configure_parser_repoquery(sub_parsers):
    help_cli = "Query repositories using mamba. "
    descr = help_cli

    example = """
Examples:

    mamba repoquery search xtensor>=0.18
    mamba repoquery depends xtensor
    mamba repoquery whoneeds xtl

    """

    import argparse
    from argparse import SUPPRESS

    p = sub_parsers.add_parser(
        "repoquery", description=descr, help=help_cli, epilog=example
    )
    subsub_parser = p.add_subparsers(dest="subcmd")
    package_cmds = argparse.ArgumentParser(add_help=False)
    package_cmds.add_argument("package_query", help="the target package")
    package_cmds.add_argument(
        "-i", "--installed", action="store_true", default=True, help=SUPPRESS
    )

    package_cmds.add_argument("-p", "--platform")
    package_cmds.add_argument("--no-installed", action="store_true")

    package_cmds.add_argument(
        "-a",
        "--all-channels",
        action="store_true",
        help="Look at all channels (for depends / whoneeds)",
    )

    view_cmds = argparse.ArgumentParser(add_help=False)
    view_cmds.add_argument("-t", "--tree", action="store_true")

    c1 = subsub_parser.add_parser(
        "whoneeds",
        help="shows packages that depends on this package",
        parents=[package_cmds, view_cmds],
    )

    c2 = subsub_parser.add_parser(
        "depends",
        help="shows packages that depends on this package",
        parents=[package_cmds, view_cmds],
    )

    c3 = subsub_parser.add_parser(
        "search",
        help="shows packages that depends on this package",
        parents=[package_cmds],
    )

    from conda.cli import conda_argparse

    for cmd in (c1, c2, c3):
        conda_argparse.add_parser_channels(cmd)
        conda_argparse.add_parser_networking(cmd)
        conda_argparse.add_parser_known(cmd)
        conda_argparse.add_parser_json(cmd)

    p.set_defaults(func=".main_repoquery.execute")
    return p


def _wrapped_main(*args, **kwargs):
    if len(args) == 1:
        args = args + ("-h",)

    argv = list(args)

    if "--zchunk" in argv:
        global use_zchunk
        use_zchunk = True
        argv.remove("--zchunk")

    if "--mamba-experimental" in argv:
        global use_mamba_experimental
        use_mamba_experimental = True
        argv.remove("--mamba-experimental")

    print_banner = True
    if "--no-banner" in argv:
        print_banner = False
        argv.remove("--no-banner")
    elif "MAMBA_NO_BANNER" in os.environ:
        print_banner = False

    args = argv

    p = generate_parser()
    configure_parser_repoquery(p._subparsers._group_actions[0])
    args = p.parse_args(args[1:])

    context.__init__(argparse_args=args)
    context.__initialized__ = True
    if print_banner and not (context.quiet or context.json):
        print(banner)

    init_loggers(context)

    result = do_call(args, p)
    exit_code = getattr(
        result, "rc", result
    )  # may be Result objects with code in rc field
    if isinstance(exit_code, int):
        return exit_code


# Main entry point!
def main(*args, **kwargs):
    # Set to false so we don't allow uploading our issues to conda!
    context.report_errors = False

    from conda.common.compat import ensure_text_type, init_std_stream_encoding

    init_std_stream_encoding()

    if "activate" in sys.argv or "deactivate" in sys.argv:
        print("Use conda to activate / deactivate the environment.")
        print("\n    $ conda " + " ".join(sys.argv[1:]) + "\n")
        return sys.exit(-1)

    if not args:
        args = sys.argv

    if "--version" in args:
        from mamba._version import __version__

        print("mamba {}".format(__version__))

    args = tuple(ensure_text_type(s) for s in args)

    if len(args) > 2 and args[1] == "env" and args[2] in ("create", "update"):
        # special handling for conda env create!
        from mamba import mamba_env

        return mamba_env.main()

    def exception_converter(*args, **kwargs):
        exit_code = 0
        try:
            exit_code = _wrapped_main(*args, **kwargs)
        except api.MambaNativeException as e:
            print(e)
        except MambaException as e:
            print(e)
        except Exception as e:
            raise e
        return exit_code

    from conda.exceptions import conda_exception_handler

    return conda_exception_handler(exception_converter, *args, **kwargs)
