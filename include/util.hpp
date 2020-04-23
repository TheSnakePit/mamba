#ifndef MAMBA_UTIL_HPP
#define MAMBA_UTIL_HPP

#include <stdexcept>
#include <string_view>


#include "thirdparty/filesystem.hpp"
namespace fs = ghc::filesystem;

namespace mamba
{
    class mamba_error : public std::runtime_error
    {
    public:
    
        using std::runtime_error::runtime_error;
    };

    bool ends_with(const std::string_view& str, const std::string_view& suffix);
    bool starts_with(const std::string_view& str, const std::string_view& prefix);
    void to_human_readable_filesize(std::ostream& o, double bytes, std::size_t precision = 0);
    bool lexists(const fs::path& p);

    class TemporaryDirectory
    {
    public:
    
        TemporaryDirectory();
        ~TemporaryDirectory();

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory&&) = default;

        fs::path& path();
        operator fs::path();

    private:
    
        fs::path m_path;
    };

    class TemporaryFile
    {
    public:

        TemporaryFile();
        ~TemporaryFile();

        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile& operator=(const TemporaryFile&) = delete;
        TemporaryFile& operator=(TemporaryFile&&) = default;

        fs::path& path();
        operator fs::path();

    private:

        fs::path m_path;
    };

}

#endif // MAMBA_UTIL_HPP
