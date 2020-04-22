# -*- coding: utf-8 -*-
# Copyright (C) 2019, QuantStack
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import absolute_import, division, print_function, unicode_literals

import sys, os

from os.path import abspath, basename, exists, isdir, isfile, join

from conda.cli import common as cli_common
from conda.cli.main import generate_parser, init_loggers
from conda.base.context import context
from conda.core.index import calculate_channel_urls, check_whitelist, _supplement_index_with_system #, get_index
from conda.models.channel import Channel, prioritize_channels
from conda.models.records import PackageRecord
from conda.models.match_spec import MatchSpec
from conda.cli.main_list import list_packages
from conda.core.prefix_data import PrefixData
from conda.misc import clone_env, explicit, touch_nonadmin
from conda.common.serialize import json_dump
from conda.cli.common import specs_from_url, confirm_yn, check_non_admin, ensure_name_or_prefix
from conda.core.subdir_data import SubdirData
from conda.core.link import UnlinkLinkTransaction, PrefixSetup
from conda.cli.install import check_prefix, clone, print_activate
from conda.base.constants import ChannelPriority, ROOT_ENV_NAME, UpdateModifier
from conda.core.solve import diff_for_unlink_link_precs
from conda.core.envs_manager import unregister_env
from conda.core.package_cache_data import PackageCacheData

# create support
from conda.common.path import paths_equal
from conda.exceptions import (CondaExitZero, CondaImportError, CondaOSError, CondaSystemExit,
                              CondaValueError, DirectoryNotACondaEnvironmentError, CondaEnvironmentError,
                              DirectoryNotFoundError, DryRunExit, EnvironmentLocationNotFound,
                              NoBaseEnvironmentError, PackageNotInstalledError, PackagesNotFoundError,
                              TooManyArgumentsError, UnsatisfiableError)

from conda.gateways.disk.create import mkdir_p
from conda.gateways.disk.delete import rm_rf, delete_trash, path_is_clean
from conda.gateways.disk.test import is_conda_environment

from conda._vendor.boltons.setutils import IndexedSet
from conda.models.prefix_graph import PrefixGraph

from logging import getLogger

import json
import tempfile
import logging

import mamba.mamba_api as api

from mamba.post_solve_handling import post_solve_handling
from mamba.utils import get_index, to_package_record_from_subjson

log = getLogger(__name__)
stderrlog = getLogger('mamba.stderr')

banner = """
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

        Supported by @QuantStack

        GitHub:  https://github.com/QuantStack/mamba
        Twitter: https://twitter.com/QuantStack

█████████████████████████████████████████████████████████████
"""

def init_api_context():
    api_ctx = api.Context()
    api_ctx.set_verbosity(context.verbosity)
    api_ctx.quiet = context.quiet
    api_ctx.json = context.json
    api_ctx.offline = context.offline
    api_ctx.local_repodata_ttl = context.local_repodata_ttl
    api_ctx.use_index_cache = context.use_index_cache
    api_ctx.always_yes = context.always_yes
    api_ctx.dry_run = context.dry_run
    if context.ssl_verify == False:
        api_ctx.ssl_verify = "<false>"
    elif context.ssl_verify is not True:
        api_ctx.ssl_verify = context.ssl_verify

    # we have to convert a frozendict here...
    proxies = {}
    for k, v in context.proxy_servers.items():
        proxies[k] = v
    api_ctx.proxies = proxies

class MambaException(Exception):
    pass

solver_options = [(api.SOLVER_FLAG_ALLOW_DOWNGRADE, 1)]

def get_installed_packages(prefix, show_channel_urls=None):
    result = {'packages': {}}

    # Currently, we need to have pip interop disabled :/
    installed = {rec: rec for rec in PrefixData(prefix, pip_interop_enabled=False).iter_records()}

    # add virtual packages as installed packages
    # they are packages installed on the system that conda can do nothing about (e.g. glibc)
    # if another version is needed, installation just fails
    # they don't exist anywhere (they start with __)
    _supplement_index_with_system(installed)
    installed = list(installed)

    for prec in installed:
        json_rec = prec.dist_fields_dump()
        json_rec['depends'] = prec.depends
        json_rec['build'] = prec.build
        result['packages'][prec.fn] = json_rec

    return installed, result


def specs_from_args(args, json=False):

    def arg2spec(arg, json=False, update=False):
        try:
            spec = MatchSpec(arg)
        except:
            from ..exceptions import CondaValueError
            raise CondaValueError('invalid package specification: %s' % arg)

        name = spec.name
        if not spec._is_simple() and update:
            from ..exceptions import CondaValueError
            raise CondaValueError("""version specifications not allowed with 'update'; use
        conda update  %s%s  or
        conda install %s""" % (name, ' ' * (len(arg) - len(name)), arg))

        return spec

    return [arg2spec(arg, json=json) for arg in args]


def to_txn(specs_to_add, specs_to_remove, prefix, to_link, to_unlink, index=None):
    to_link_records, to_unlink_records = [], []

    prefix_data = PrefixData(prefix)
    final_precs = IndexedSet(prefix_data.iter_records())

    def get_channel(c):
        for _, chan in index:
            if str(chan) == c:
                return chan

    for c, pkg in to_unlink:
        for i_rec in installed_pkg_recs:
            if i_rec.fn == pkg:
                final_precs.remove(i_rec)
                to_unlink_records.append(i_rec)
                break
        else:
            print("No package record found!")

    for c, pkg, jsn_s in to_link:
        sdir = get_channel(c)
        rec = to_package_record_from_subjson(sdir, pkg, jsn_s)
        final_precs.add(rec)
        to_link_records.append(rec)

    final_precs, specs_to_add, specs_to_remove = post_solve_handling(context, prefix_data, final_precs, specs_to_add, specs_to_remove)
    unlink_precs, link_precs = diff_for_unlink_link_precs(prefix,
                                                          final_precs=IndexedSet(PrefixGraph(final_precs).graph),
                                                          specs_to_add=specs_to_add,
                                                          force_reinstall=context.force_reinstall)

    pref_setup = PrefixSetup(
        target_prefix  = prefix,
        unlink_precs   = unlink_precs,
        link_precs     = link_precs,
        remove_specs   = specs_to_remove,
        update_specs   = specs_to_add,
        neutered_specs = ()
    )

    conda_transaction = UnlinkLinkTransaction(pref_setup)
    return conda_transaction

use_mamba_download = False

def handle_txn(unlink_link_transaction, prefix, args, newenv, remove_op=False):
    if unlink_link_transaction.nothing_to_do:
        if remove_op:
            # No packages found to remove from environment
            raise PackagesNotFoundError(args.package_names)
        elif not newenv:
            if context.json:
                cli_common.stdout_json_success(message='All requested packages already installed.')
            else:
                print('\n# All requested packages already installed.\n')
            return

    # don't confirm / print if using mamba for download
    if not context.json and not use_mamba_download:
        unlink_link_transaction.print_transaction_summary()
        confirm_yn()

    elif context.dry_run:
        actions = unlink_link_transaction._make_legacy_action_groups()[0]
        cli_common.stdout_json_success(prefix=prefix, actions=actions, dry_run=True)
        raise DryRunExit()

    try:
        unlink_link_transaction.download_and_extract()
        if context.download_only:
            raise CondaExitZero('Package caches prepared. UnlinkLinkTransaction cancelled with '
                                '--download-only option.')
        unlink_link_transaction.execute()

    except SystemExit as e:
        raise CondaSystemExit('Exiting', e)

    if newenv:
        touch_nonadmin(prefix)
        print_activate(args.name if args.name else prefix)

    if context.json:
        actions = unlink_link_transaction._make_legacy_action_groups()[0]
        cli_common.stdout_json_success(prefix=prefix, actions=actions)

installed_pkg_recs = None

def get_installed_jsonfile(prefix):
    global installed_pkg_recs
    installed_pkg_recs, output = get_installed_packages(prefix, show_channel_urls=True)
    installed_json_f = tempfile.NamedTemporaryFile('w', delete=False)
    installed_json_f.write(json_dump(output))
    installed_json_f.flush()
    return installed_json_f


def remove(args, parser):
    if not (args.all or args.package_names):
        raise CondaValueError('no package names supplied,\n'
                              '       try "mamba remove -h" for more details')

    prefix = context.target_prefix
    check_non_admin()
    init_api_context()

    if args.all and prefix == context.default_prefix:
        raise CondaEnvironmentError("cannot remove current environment. \
                                     deactivate and run mamba remove again")

    if args.all and path_is_clean(prefix):
        # full environment removal was requested, but environment doesn't exist anyway
        return 0

    if args.all:
        if prefix == context.root_prefix:
            raise CondaEnvironmentError('cannot remove root environment,\n'
                                        '       add -n NAME or -p PREFIX option')
        print("\nRemove all packages in environment %s:\n" % prefix, file=sys.stderr)

        if 'package_names' in args:
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
                print("No packages found in %s. Continuing environment removal" % prefix)

        rm_rf(prefix, clean_empty_parents=True)
        unregister_env(prefix)

        return

    else:
        if args.features:
            specs = tuple(MatchSpec(track_features=f) for f in set(args.package_names))
        else:
            specs = [s for s in specs_from_args(args.package_names)]
        if not (context.quiet or context.json):
            print("Removing specs: {}".format(specs))
        channel_urls = ()
        subdirs = ()

        installed_json_f = get_installed_jsonfile(prefix)

        mamba_solve_specs = [s.conda_build_form() for s in specs]

        solver_options.append((api.SOLVER_FLAG_ALLOW_UNINSTALL, 1))

        pool = api.Pool()
        repos = []

        # add installed
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

        transaction = api.Transaction(solver)
        to_link, to_unlink = transaction.to_conda()

        conda_transaction = to_txn((), specs, prefix, to_link, to_unlink)
        handle_txn(conda_transaction, prefix, args, False, True)

def install(args, parser, command='install'):
    """
    mamba install, mamba update, and mamba create
    """
    context.validate_configuration()
    check_non_admin()

    init_api_context()

    newenv = bool(command == 'create')
    isinstall = bool(command == 'install')
    solver_task = api.SOLVER_INSTALL

    isupdate = bool(command == 'update')
    if isupdate:
        solver_task = api.SOLVER_UPDATE

    if newenv:
        ensure_name_or_prefix(args, command)
    prefix = context.target_prefix
    if newenv:
        check_prefix(prefix, json=context.json)
    if context.force_32bit and prefix == context.root_prefix:
        raise CondaValueError("cannot use CONDA_FORCE_32BIT=1 in base env")
    if isupdate and not (args.file or args.packages
                         or context.update_modifier == UpdateModifier.UPDATE_ALL):
        raise CondaValueError("""no package names supplied
# If you want to update to a newer version of Anaconda, type:
#
# $ conda update --prefix %s anaconda
""" % prefix)

    if not newenv:
        if isdir(prefix):
            delete_trash(prefix)
            if not isfile(join(prefix, 'conda-meta', 'history')):
                if paths_equal(prefix, context.conda_prefix):
                    raise NoBaseEnvironmentError()
                else:
                    if not path_is_clean(prefix):
                        raise DirectoryNotACondaEnvironmentError(prefix)
            else:
                # fall-through expected under normal operation
                pass
        else:
            if args.mkdir:
                try:
                    mkdir_p(prefix)
                except EnvironmentError as e:
                    raise CondaOSError("Could not create directory: %s" % prefix, caused_by=e)
            else:
                raise EnvironmentLocationNotFound(prefix)

    prepend = not args.override_channels
    prefix = context.target_prefix

    index_args = {
        'use_cache': args.use_index_cache,
        'channel_urls': context.channels,
        'unknown': args.unknown,
        'prepend': not args.override_channels,
        'use_local': args.use_local
    }

    args_packages = [s.strip('"\'') for s in args.packages]
    if newenv and not args.no_default_packages:
        # Override defaults if they are specified at the command line
        # TODO: rework in 4.4 branch using MatchSpec
        args_packages_names = [pkg.replace(' ', '=').split('=', 1)[0] for pkg in args_packages]
        for default_pkg in context.create_default_packages:
            default_pkg_name = default_pkg.replace(' ', '=').split('=', 1)[0]
            if default_pkg_name not in args_packages_names:
                args_packages.append(default_pkg)

    num_cp = sum(s.endswith('.tar.bz2') for s in args_packages)
    if num_cp:
        if num_cp == len(args_packages):
            explicit(args_packages, prefix, verbose=not (context.quiet or context.json))
            return
        else:
            raise CondaValueError("cannot mix specifications with conda package"
                                  " filenames")

    index = get_index(channel_urls=index_args['channel_urls'],
                      prepend=index_args['prepend'], platform=None,
                      use_local=index_args['use_local'], use_cache=index_args['use_cache'],
                      unknown=index_args['unknown'], prefix=prefix)

    channel_json = []
    strict_priority = (context.channel_priority == ChannelPriority.STRICT)

    if strict_priority:
        # first, count unique channels
        n_channels = len(set([channel.canonical_name for _, channel in index]))
        current_channel = index[0][1].canonical_name
        channel_prio = n_channels

    for subdir, chan in index:
        # add priority here
        if strict_priority:
            if chan.canonical_name != current_channel:
                channel_prio -= 1
                current_channel = chan.canonical_name
            priority = channel_prio
        else:
            priority = 0

        subpriority = 0 if chan.platform == 'noarch' else 1

        if context.verbosity != 0:
            print("Cache path: ", subdir.cache_path())
        channel_json.append((chan, subdir.cache_path(), priority, subpriority))

    installed_json_f = get_installed_jsonfile(prefix)

    specs = []

    if args.file:
        for fpath in args.file:
            try:
                file_specs = specs_from_url(fpath, json=context.json)
            except Unicode:
                raise CondaValueError("Error reading file, file should be a text file containing"
                                 " packages \nconda create --help for details")
        if '@EXPLICIT' in file_specs:
            explicit(file_specs, prefix, verbose=not (context.quiet or context.json), index_args=index_args)
            return
        specs.extend([MatchSpec(s) for s in file_specs])

    specs.extend(specs_from_args(args_packages, json=context.json))

    if isinstall and args.revision:
        get_revision(args.revision, json=context.json)
    elif isinstall and not (args.file or args_packages):
        raise CondaValueError("too few arguments, "
                              "must supply command line package specs or --file")

    # for 'conda update', make sure the requested specs actually exist in the prefix
    # and that they are name-only specs
    if isupdate and context.update_modifier == UpdateModifier.UPDATE_ALL:
        # Note: History(prefix).get_requested_specs_map()
        print("Currently, mamba can only update explicit packages! (e.g. mamba update numpy python ...)")
        exit()

    if isupdate and context.update_modifier != UpdateModifier.UPDATE_ALL:
        prefix_data = PrefixData(prefix)
        for s in args_packages:
            s = MatchSpec(s)
            if not s.is_name_only_spec:
                raise CondaValueError("Invalid spec for 'conda update': %s\n"
                                 "Use 'conda install' instead." % s)
            if not prefix_data.get(s.name, None):
                raise PackageNotInstalledError(prefix, s.name)

    if newenv and args.clone:
        if args.packages:
            raise TooManyArgumentsError(0, len(args.packages), list(args.packages),
                                        'did not expect any arguments for --clone')

        clone(args.clone, prefix, json=context.json, quiet=(context.quiet or context.json), index_args=index_args)
        touch_nonadmin(prefix)
        print_activate(args.name if args.name else prefix)
        return

    # If python was not specified, check if it is installed.
    # If yes, add the installed python to the specs to prevent updating it.
    spec_names = [s.name for s in specs]
    if 'python' not in spec_names:
        installed_names = [i_rec.name for i_rec in installed_pkg_recs]
        if 'python' in installed_names:
            i = installed_names.index('python')
            version = installed_pkg_recs[i].version
            specs.append(MatchSpec('python==' + version))

    mamba_solve_specs = [s.conda_build_form() for s in specs]

    if not (context.quiet or context.json):
        print("\nLooking for: {}\n".format(mamba_solve_specs))

    pool = api.Pool()

    repos = []

    # add installed
    repo = api.Repo(pool, "installed", installed_json_f.name, "")
    repo.set_installed()
    repos.append(repo)

    for channel, cache_file, priority, subpriority in channel_json:
        repo = api.Repo(pool, str(channel), cache_file, channel.url(with_credentials=True))
        repo.set_priority(priority, subpriority)
        repos.append(repo)

    solver = api.Solver(pool, solver_options)
    solver.add_jobs(mamba_solve_specs, solver_task)
    success = solver.solve()
    if not success:
        print(solver.problems_to_str())
        exit_code = 1
        return exit_code

    transaction = api.Transaction(solver)
    to_link, to_unlink = transaction.to_conda()

    if use_mamba_download:
        downloaded = transaction.prompt(PackageCacheData.first_writable().pkgs_dir, repos)
        if not downloaded:
            exit(0)
        PackageCacheData.first_writable().reload()

    conda_transaction = to_txn(specs, (), prefix, to_link, to_unlink, index)
    handle_txn(conda_transaction, prefix, args, newenv)

    try:
        installed_json_f.close()
        os.unlink(installed_json_f.name)
    except:
        pass

def create(args, parser):
    if is_conda_environment(context.target_prefix):
        if paths_equal(context.target_prefix, context.root_prefix):
            raise CondaValueError("The target prefix is the base prefix. Aborting.")
        confirm_yn("WARNING: A conda environment already exists at '%s'\n"
                   "Remove existing environment" % context.target_prefix,
                   default='no',
                   dry_run=False)
        log.info("Removing existing environment %s", context.target_prefix)
        rm_rf(context.target_prefix)
    elif isdir(context.target_prefix):
        confirm_yn("WARNING: A directory already exists at the target location '%s'\n"
                   "but it is not a conda environment.\n"
                   "Continue creating environment" % context.target_prefix,
                   default='no',
                   dry_run=False)
    install(args, parser, 'create')

def update(args, parser):
    if context.force:
        print("\n\n"
              "WARNING: The --force flag will be removed in a future conda release.\n"
              "         See 'conda update --help' for details about the --force-reinstall\n"
              "         and --clobber flags.\n"
              "\n", file=sys.stderr)

    # need to implement some modifications on the update function
    install(args, parser, 'update')

def repoquery(args, parser):
    prepend = not args.override_channels
    prefix = context.target_prefix

    init_api_context()

    index_args = {
        'use_cache': args.use_index_cache,
        'channel_urls': context.channels,
        'unknown': args.unknown,
        'prepend': not args.override_channels,
        'use_local': args.use_local
    }

    index = get_index(channel_urls=index_args['channel_urls'],
                  prepend=index_args['prepend'], platform=None,
                  use_local=index_args['use_local'], use_cache=index_args['use_cache'],
                  unknown=index_args['unknown'], prefix=prefix)

    installed_json_f = get_installed_jsonfile(prefix)

    pool = api.Pool()
    repos = []

    # add installed
    repo = api.Repo(pool, "installed", installed_json_f.name, "")
    repo.set_installed()
    repos.append(repo)

    if not args.installed:
        for subdir, channel in index:
            repo = api.Repo(pool, str(channel), subdir.cache_path(), channel.url(with_credentials=True))
            repo.set_priority(0, 0)
            repos.append(repo)


    print("\nExecuting the query %s\n" % args.query)

    query = api.Query(pool)
    if args.whatrequires:
        print(query.whatrequires(args.query))
    else:
        print(query.find(args.query))


def do_call(args, parser):
    relative_mod, func_name = args.func.rsplit('.', 1)

    # func_name should always be 'execute'
    if relative_mod in ['.main_list', '.main_search', '.main_run', '.main_clean', '.main_info']:
        from importlib import import_module
        module = import_module('conda.cli' + relative_mod, __name__.rsplit('.', 1)[0])
        exit_code = getattr(module, func_name)(args, parser)
    elif relative_mod == '.main_install':
        exit_code = install(args, parser, 'install')
    elif relative_mod == '.main_remove':
        exit_code = remove(args, parser)
    elif relative_mod == '.main_create':
        exit_code = create(args, parser)
    elif relative_mod == '.main_update':
        exit_code = update(args, parser)
    elif relative_mod == '.main_repoquery':
        exit_code = repoquery(args, parser)
    else:
        print("Currently, only install, create, list, search, run, info and clean are supported through mamba.")

        return 0
    return exit_code

def configure_parser_repoquery(sub_parsers):
    help = "Query repositories using mamba. "
    descr = (help)

    example = ("""
Examples:

    conda repoquery xtensor>=0.18

    """)

    from argparse import SUPPRESS

    p = sub_parsers.add_parser(
        'repoquery',
        description=descr,
        help=help,
        epilog=example,
    )

    p.add_argument(
        'query',
        action="store",
        nargs='?',
        help="Package query.",
    )

    p.add_argument(
        "--whatrequires",
        action="store_true",
        help=SUPPRESS,
    )

    p.add_argument(
        "--installed",
        action="store_true",
        help=SUPPRESS,
    )

    from conda.cli import conda_argparse
    conda_argparse.add_parser_channels(p)
    conda_argparse.add_parser_networking(p)
    conda_argparse.add_parser_known(p)

    p.set_defaults(func='.main_repoquery.execute')
    return p


def _wrapped_main(*args, **kwargs):
    if len(args) == 1:
        args = args + ('-h',)

    import copy
    argv = list(args)
    if "--mamba-download" in argv:
        global use_mamba_download
        use_mamba_download = True
        argv.remove('--mamba-download')
        args = argv

    p = generate_parser()
    configure_parser_repoquery(p._subparsers._group_actions[0])
    args = p.parse_args(args[1:])

    context.__init__(argparse_args=args)
    if not (context.quiet or context.json):
        print(banner)

    init_loggers(context)

    exit_code = do_call(args, p)
    if isinstance(exit_code, int):
        return exit_code

# Main entry point!
def main(*args, **kwargs):
    # Set to false so we don't allow uploading our issues to conda!
    context.report_errors = False

    from conda.common.compat import ensure_text_type, init_std_stream_encoding
    init_std_stream_encoding()

    if 'activate' in sys.argv or 'deactivate' in sys.argv:
        print("Use conda to activate / deactivate the environment.")
        print('\n    $ conda ' + ' '.join(sys.argv[1:]) + '\n')
        return sys.exit(-1)

    if not args:
        args = sys.argv

    args = tuple(ensure_text_type(s) for s in args)

    if len(args) > 2 and args[1] == 'env' and args[2] == 'create':
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
