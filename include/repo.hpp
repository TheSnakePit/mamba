#ifndef MAMBA_REPO_HPP
#define MAMBA_REPO_HPP

#include <string>
#include <tuple>

#include "prefix_data.hpp"

extern "C"
{
    #include "common_write.c"

    #include "solv/repo.h"
    #include "solv/repo_solv.h"
    #include "solv/conda.h"
    #include "solv/repo_conda.h"
}

#include "pool.hpp"

namespace mamba
{
    class MRepo
    {
    public:

        MRepo(MPool& pool, const PrefixData& prefix_data);
        MRepo(MPool& pool, const std::string& name,
              const std::string& filename, const std::string& url);
        ~MRepo();

        void set_installed();
        void set_priority(int priority, int subpriority);

        const char* name() const;
        const std::string& url() const;
        Repo* repo();
        std::tuple<int, int> priority() const;
        std::size_t size() const;

    private:

        bool read_file(const std::string& filename);

        std::string m_json_file, m_solv_file;
        std::string m_url;

        Repo* m_repo;
    };
}

#endif // MAMBA_REPO_HPP
