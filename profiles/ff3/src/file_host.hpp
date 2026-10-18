#pragma once
#include "psprecomp/runtime.hpp"
#include <cstdio>
#include <map>
#include <set>
namespace ff3 {
class FileHost {
public:
    explicit FileHost(std::filesystem::path disc);
    ~FileHost();
    void install(psprecomp::Runtime &);
private:
    struct File { std::FILE *stream{}; std::int64_t result{}; std::uint64_t base{},length{}; };
    struct Directory { std::vector<std::filesystem::path> entries; std::size_t cursor{}; };
    std::filesystem::path path(const std::string &guest,bool write=false) const;
    bool stat(psprecomp::Runtime &,std::uint32_t,const std::filesystem::path &);
    std::filesystem::path disc_,memory_stick_,iso_;
    std::map<std::uint32_t,File> files_;
    std::map<std::uint32_t,Directory> directories_;
    std::set<std::uint32_t> stick_callbacks_;
    std::uint32_t umd_callback_{},umd_state_{0x32};
    bool umd_replace_allowed_{true};
    std::uint32_t next_fd_{1000};
};
}
