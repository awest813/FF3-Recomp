#pragma once
#include "bootstrap_hle.hpp"
#include <array>
namespace ff3 {
// Timed PCM output with an optional host-device sink and raw diagnostic capture.
class AudioHost {
public:
    void install(psprecomp::Runtime &,BootstrapHle &);
    using Output=std::function<void(unsigned,std::span<const std::int16_t>)>;
    void set_output(Output output,std::function<void(unsigned)> reset) {output_=std::move(output);reset_=std::move(reset);}
private:
    struct Channel { bool reserved{}; std::uint32_t samples{},format{},left{0x8000},right{0x8000}; std::uint64_t busy_until{}; };
    std::array<Channel,8> channels_{};
    Output output_;
    std::function<void(unsigned)> reset_;
};
}
