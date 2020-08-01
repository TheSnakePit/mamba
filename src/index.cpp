#include "thirdparty/filesystem.hpp"

#include "index.hpp"
#include "fetch.hpp"

namespace fs = ghc::filesystem;

namespace mamba
{

    index_type get_index(const std::vector<std::string> channel_names,
                         bool append_context_channels,
                         const std::string& platform,
                         bool use_local,
                         bool use_cache,
                         bool unknown,
                         const std::string& prefix,
                         const std::string& repodata_fn)
    {
        std::vector<std::string> channel_urls = calculate_channel_urls(channel_names,
                                                                       append_context_channels,
                                                                       platform,
                                                                       use_local);
        check_whitelist(channel_urls);

        MultiDownloadTarget dlist;
        index_type index;
        index.reserve(channel_urls.size());

        for (const auto& url: channel_urls)
        {
            Channel& channel = make_channel(url);
            std::string full_url = channel.url() + '/' + repodata_fn;
            fs::path full_path_cache = fs::path(create_cache_dir()) / fs::path(cache_fn_url(full_url));
            MSubdirData sd(channel.name() + '/' + channel.platform(), full_url, full_path_cache);
            index.push_back(std::make_pair(std::move(sd), channel));
            index.back().first.load();
            dlist.add(index.back().first.target());
        }

        if (!dlist.download(true))
        {
            throw std::runtime_error("Error downloading repodata.");
        }

        return index;
    }
}

