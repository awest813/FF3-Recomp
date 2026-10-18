#pragma once
#include "bootstrap_hle.hpp"
#include "vcs_media_decoder.hpp"
#include <map>
namespace ff3 {
class MovieHost {
public:
    explicit MovieHost(std::filesystem::path disc):disc_(std::move(disc)){}
    void install(psprecomp::Runtime &,BootstrapHle &);
private:
    struct Player {
        std::uint32_t status{1},format{3},loop{1},width{},height{},duration{},frame{},play_mode{};
        std::uint64_t next_frame{};
        std::filesystem::path path;
        vcs::VideoStreamDecoder video;
        vcs::PmfAudioDecoder audio;
        std::vector<std::uint8_t> rgba;
        bool ready{};
    };
    std::map<std::uint32_t,Player> players_;
    std::filesystem::path disc_;
    bool initialized_{};
};
}
