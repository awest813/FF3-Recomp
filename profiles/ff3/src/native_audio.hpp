#pragma once
#include "pcm_mixer.hpp"
#include <memory>
namespace ff3 {
class NativeAudio {
public:
    NativeAudio();
    ~NativeAudio();
    void submit(unsigned channel,std::span<const std::int16_t> pcm);
    void clear(unsigned channel);
    void volume(unsigned percent);
    void enable(bool enabled);
    PcmMixer::Stats stats();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
