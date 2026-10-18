#pragma once
#include "bootstrap_hle.hpp"
#include <filesystem>
namespace ff3 {
class FontModule {
public:
    explicit FontModule(std::filesystem::path image):image_(std::move(image)){}
    void install(psprecomp::Runtime &,BootstrapHle &);
private:
    struct Caller {std::uint32_t ra,gp;};
    std::filesystem::path image_;
    std::map<std::uint32_t,std::string> registries_,categories_;
    std::map<std::uint32_t,std::vector<std::uint8_t>> keys_;
    std::int32_t uid_{};
    bool started_{};
    std::map<std::uint32_t,std::string> media_modules_;
    std::map<std::int32_t,std::vector<Caller>> callers_;
};
}
