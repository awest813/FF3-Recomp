#include "native_audio.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <iostream>

namespace ff3 {
struct NativeAudio::State {
    PcmMixer mixer;
    AudioQueueRef queue{};
    std::atomic<bool> closing{false};
    static constexpr unsigned buffer_bytes=1024*2*sizeof(std::int16_t);
    static void output(void *context,AudioQueueRef queue,AudioQueueBufferRef buffer) {
        auto &s=*static_cast<State *>(context);
        if(s.closing)return;
        s.mixer.render({static_cast<std::int16_t *>(buffer->mAudioData),buffer_bytes/2});
        buffer->mAudioDataByteSize=buffer_bytes;
        AudioQueueEnqueueBuffer(queue,buffer,0,nullptr);
    }
};
NativeAudio::NativeAudio():state_(std::make_unique<State>()) {
    AudioStreamBasicDescription format{};
    format.mSampleRate=44100;format.mFormatID=kAudioFormatLinearPCM;
    format.mFormatFlags=kLinearPCMFormatFlagIsSignedInteger|kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket=4;format.mFramesPerPacket=1;format.mBytesPerFrame=4;
    format.mChannelsPerFrame=2;format.mBitsPerChannel=16;
    auto result=AudioQueueNewOutput(&format,State::output,state_.get(),nullptr,nullptr,0,&state_->queue);
    if(!result)for(unsigned i=0;i<3;++i) {
        AudioQueueBufferRef buffer{};
        result=AudioQueueAllocateBuffer(state_->queue,State::buffer_bytes,&buffer);
        if(result)break;
        State::output(state_.get(),state_->queue,buffer);
    }
    if(!result)result=AudioQueueStart(state_->queue,nullptr);
    if(result) {
        state_->closing=true;
        if(state_->queue)AudioQueueDispose(state_->queue,true);
        state_->queue=nullptr;
        std::cerr<<"[audio-device] unavailable, OSStatus="<<result<<"\n";
    } else std::cerr<<"[audio-device] started: 44100 Hz stereo, 3 x 1024-frame buffers\n";
}
NativeAudio::~NativeAudio() {
    state_->closing=true;
    if(state_->queue)AudioQueueDispose(state_->queue,true);
    const auto s=state_->mixer.stats();
    std::cerr<<"[audio-device] submitted="<<s.submitted<<" played="<<s.played
             <<" empty="<<s.empty<<" dropped="<<s.dropped<<" nonzero="<<s.nonzero<<" frames\n";
}
void NativeAudio::submit(unsigned channel,std::span<const std::int16_t> pcm) {
    if(state_->queue)state_->mixer.submit(channel,pcm);
}
void NativeAudio::volume(unsigned percent) {state_->mixer.volume(percent);}
void NativeAudio::clear(unsigned channel) {state_->mixer.clear(channel);}
void NativeAudio::enable(bool enabled) {state_->mixer.enable(enabled);}
PcmMixer::Stats NativeAudio::stats() {return state_->mixer.stats();}
}
