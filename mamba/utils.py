# Copyright (C) 2019, QuantStack
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import absolute_import, division, print_function, unicode_literals

import json
import os
import tempfile

from conda._vendor.boltons.setutils import IndexedSet
from conda.base.constants import ChannelPriority
from conda.base.context import context
from conda.common.serialize import json_dump
from conda.common.url import join_url, split_anaconda_token
from conda.core.index import (
    _supplement_index_with_system,
    calculate_channel_urls,
    check_whitelist,
)
from conda.core.link import PrefixSetup, UnlinkLinkTransaction
from conda.core.prefix_data import PrefixData
from conda.core.solve import diff_for_unlink_link_precs
from conda.gateways.connection.session import CondaHttpAuth
from conda.models.channel import Channel
from conda.models.prefix_graph import PrefixGraph
from conda.models.records import PackageRecord

import mamba.mamba_api as api


def load_channel(subdir_data, result_container):
    if not context.quiet:
        print("Getting ", subdir_data.channel.name, subdir_data.channel.platform)
    return result_container.append(subdir_data.load())


def get_index(
    channel_urls=(),
    prepend=True,
    platform=None,
    use_local=False,
    use_cache=False,
    unknown=None,
    prefix=None,
    repodata_fn="repodata.json",
):

    real_urls = calculate_channel_urls(channel_urls, prepend, platform, use_local)
    check_whitelist(real_urls)

    dlist = api.DownloadTargetList()

    index = []

    for url in real_urls:
        channel = Channel(url)
        full_url = CondaHttpAuth.add_binstar_token(
            channel.url(with_credentials=True) + "/" + repodata_fn
        )

        full_path_cache = os.path.join(
            api.create_cache_dir(), api.cache_fn_url(full_url)
        )
        if channel.name:
            channel_name = channel.name + "/" + channel.subdir
        else:
            channel_name = channel.url(with_credentials=False)
        sd = api.SubdirData(channel_name, full_url, full_path_cache)

        sd.load()
        index.append((sd, channel))
        dlist.add(sd)

    is_downloaded = dlist.download(True)

    if not is_downloaded:
        raise RuntimeError("Error downloading repodata.")

    return index


def load_channels(
    pool,
    channels,
    repos,
    strict_priority=None,
    prepend=True,
    platform=None,
    use_local=False,
    use_cache=True,
    repodata_fn="repodata.json",
):
    index = get_index(
        channel_urls=channels,
        prepend=prepend,
        platform=platform,
        use_local=use_local,
        repodata_fn=repodata_fn,
        use_cache=use_cache,
    )

    if strict_priority is None:
        strict_priority = context.channel_priority == ChannelPriority.STRICT

    subprio_index = len(index)
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
        if strict_priority:
            subpriority = 0 if chan.platform == "noarch" else 1
        else:
            subpriority = subprio_index
            subprio_index -= 1

        if not subdir.loaded() and chan.platform != "noarch":
            # ignore non-loaded subdir if channel is != noarch
            continue

        if context.verbosity != 0:
            print("Channel: {}, prio: {} : {}".format(chan, priority, subpriority))
            print("Cache path: ", subdir.cache_path())

        repo = subdir.create_repo(pool)
        repo.set_priority(priority, subpriority)
        repos.append(repo)

    return index


def init_api_context(use_mamba_experimental=False, use_zchunk=False):
    api_ctx = api.Context()

    api_ctx.use_zchunk = use_zchunk
    api_ctx.json = context.json
    api_ctx.dry_run = context.dry_run
    if context.json:
        context.always_yes = True
        context.quiet = True
        if use_mamba_experimental:
            context.json = False
    api_ctx.set_verbosity(context.verbosity)
    api_ctx.quiet = context.quiet
    api_ctx.offline = context.offline
    api_ctx.local_repodata_ttl = context.local_repodata_ttl
    api_ctx.use_index_cache = context.use_index_cache
    api_ctx.always_yes = context.always_yes
    api_ctx.channels = context.channels

    if context.ssl_verify is False:
        api_ctx.ssl_verify = "<false>"
    elif context.ssl_verify is None or context.ssl_verify == "":
        api_ctx.ssl_verify = context.ssl_verify
    api_ctx.target_prefix = context.target_prefix
    api_ctx.root_prefix = context.root_prefix
    api_ctx.conda_prefix = context.conda_prefix
    api_ctx.pkgs_dirs = context.pkgs_dirs
    api_ctx.envs_dirs = context.envs_dirs

    api_ctx.connect_timeout_secs = int(round(context.remote_connect_timeout_secs))
    api_ctx.max_retries = context.remote_max_retries
    api_ctx.retry_backoff = context.remote_backoff_factor
    api_ctx.add_pip_as_python_dependency = context.add_pip_as_python_dependency


def to_package_record_from_subjson(channel, pkg, jsn_string):
    channel = channel
    channel_url = channel.url(with_credentials=True)
    info = json.loads(jsn_string)
    info["fn"] = pkg
    info["channel"] = channel
    info["url"] = join_url(channel_url, pkg)
    package_record = PackageRecord(**info)
    return package_record


def get_installed_packages(prefix, show_channel_urls=None):
    result = {"packages": {}}

    # Currently, we need to have pip interop disabled :/
    installed = {
        rec: rec for rec in PrefixData(prefix, pip_interop_enabled=False).iter_records()
    }

    # add virtual packages as installed packages
    # they are packages installed on the system that conda can do nothing
    # about (e.g. glibc)
    # if another version is needed, installation just fails
    # they don't exist anywhere (they start with __)
    _supplement_index_with_system(installed)
    installed = list(installed)

    for prec in installed:
        json_rec = prec.dist_fields_dump()
        json_rec["depends"] = prec.depends
        json_rec["build"] = prec.build
        result["packages"][prec.fn] = json_rec

    return installed, result


installed_pkg_recs = None


def get_installed_jsonfile(prefix):
    global installed_pkg_recs
    installed_pkg_recs, output = get_installed_packages(prefix, show_channel_urls=True)
    installed_json_f = tempfile.NamedTemporaryFile("w", delete=False)
    installed_json_f.write(json_dump(output))
    installed_json_f.flush()
    return installed_json_f, installed_pkg_recs


def to_txn(
    specs_to_add,
    specs_to_remove,
    prefix,
    to_link,
    to_unlink,
    installed_pkg_recs,
    index=None,
):
    if index is None:
        index = []
    to_link_records, to_unlink_records = [], []

    prefix_data = PrefixData(prefix)
    final_precs = IndexedSet(prefix_data.iter_records())

    lookup_dict = {}
    for _, c in index:
        lookup_dict[c.url(with_credentials=False)] = c

    for _, pkg in to_unlink:
        for i_rec in installed_pkg_recs:
            if i_rec.fn == pkg:
                final_precs.remove(i_rec)
                to_unlink_records.append(i_rec)
                break
        else:
            print("No package record found!")

    for c, pkg, jsn_s in to_link:
        sdir = lookup_dict[split_anaconda_token(c)[0]]
        rec = to_package_record_from_subjson(sdir, pkg, jsn_s)
        final_precs.add(rec)
        to_link_records.append(rec)

    unlink_precs, link_precs = diff_for_unlink_link_precs(
        prefix,
        final_precs=IndexedSet(PrefixGraph(final_precs).graph),
        specs_to_add=specs_to_add,
        force_reinstall=context.force_reinstall,
    )

    pref_setup = PrefixSetup(
        target_prefix=prefix,
        unlink_precs=unlink_precs,
        link_precs=link_precs,
        remove_specs=specs_to_remove,
        update_specs=specs_to_add,
        neutered_specs=(),
    )

    conda_transaction = UnlinkLinkTransaction(pref_setup)
    return conda_transaction
