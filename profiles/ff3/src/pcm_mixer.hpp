#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace ff3 {
// Per-channel FIFO preserves PSP channel ordering. The device callback never
// allocates or touches guest RAM; clipping happens only after summing channels.
class PcmMixer {
public:
    static constexpr std::size_t capacity=65536;
    PcmMixer() { for(auto &c:channels_) c.samples.resize(capacity*2); }
    void enable(bool enabled) {
        std::lock_guard lock(mutex_);
        if(enabled_==enabled)return;
        enabled_=enabled;for(auto &c:channels_)c.read=c.size=0;
    }
    void volume(unsigned percent) {
        std::lock_guard lock(mutex_);
        target_gain_=static_cast<int>(std::min(percent,100u)*32768/100);
    }
    void clear(unsigned channel) {
        std::lock_guard lock(mutex_);
        if(channel<channels_.size())channels_[channel].read=channels_[channel].size=0;
    }
    void submit(unsigned channel,std::span<const std::int16_t> stereo) {
        std::lock_guard lock(mutex_);
        if(!enabled_||channel>=channels_.size())return;
        auto &c=channels_[channel];
        for(std::size_t i=0;i+1<stereo.size();i+=2) {
            if(c.size==capacity){c.read=(c.read+1)%capacity;--c.size;++dropped_;}
            const auto index=(c.read+c.size)%capacity;
            c.samples[index*2]=stereo[i];c.samples[index*2+1]=stereo[i+1];++c.size;
        }
        submitted_+=stereo.size()/2;
    }
    void render(std::span<std::int16_t> stereo) {
        std::lock_guard lock(mutex_);
        std::fill(stereo.begin(),stereo.end(),0);
        if(!enabled_)return;
        for(std::size_t i=0;i+1<stereo.size();i+=2) {
            std::int32_t left=0,right=0;bool present=false;
            for(auto &c:channels_)if(c.size){
                left+=c.samples[c.read*2];right+=c.samples[c.read*2+1];
                c.read=(c.read+1)%capacity;--c.size;present=true;
            }
            // About 5 ms for a full-scale gain transition at 44.1 kHz.
            gain_+=std::clamp(target_gain_-gain_,-149,149);
            left=static_cast<std::int32_t>(std::int64_t(left)*gain_/32768);
            right=static_cast<std::int32_t>(std::int64_t(right)*gain_/32768);
            stereo[i]=static_cast<std::int16_t>(std::clamp(left,-32768,32767));
            stereo[i+1]=static_cast<std::int16_t>(std::clamp(right,-32768,32767));
            if(stereo[i]||stereo[i+1])++nonzero_;
            if(present)++played_;else ++empty_;
        }
    }
    struct Stats { std::uint64_t submitted,played,empty,dropped,nonzero; };
    Stats stats() { std::lock_guard lock(mutex_);return {submitted_,played_,empty_,dropped_,nonzero_}; }
private:
    struct Channel { std::vector<std::int16_t> samples;std::size_t read{},size{}; };
    std::array<Channel,8> channels_;
    std::mutex mutex_;
    bool enabled_{true};
    int gain_{32768},target_gain_{32768};
    std::uint64_t submitted_{},played_{},empty_{},dropped_{},nonzero_{};
};
}
