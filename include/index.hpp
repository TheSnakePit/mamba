#ifndef MAMBA_INDEX_HPP
#define MAMBA_INDEX_HPP

#include <string>
#include <utility>
#include <vector>

#include "channel.hpp"
#include "subdirdata.hpp"

namespace mamba
{
    using index_type = std::vector<std::pair<MSubdirData, Channel>>;

    index_type get_index(const std::vector<std::string> channel_names,
                         bool append_context_channels = true,
                         const std::string& platform = "",
                         bool use_local = false,
                         bool use_cache = false,
                         bool unknown = false,
                         const std::string& prefix = "",
                         const std::string& repodata_fn = "repodata.json");

}

#endif

