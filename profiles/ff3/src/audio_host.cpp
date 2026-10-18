#include "audio_host.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
namespace ff3 {
void AudioHost::install(psprecomp::Runtime &rt,BootstrapHle &hle) {
    rt.register_hle("sceAudio",0x5EC81C55u,[this](auto &r,auto &c){
        auto id=c.gpr[4];const auto samples=c.gpr[5],format=c.gpr[6];
        if(id==UINT32_MAX){id=0;while(id<8&&channels_[id].reserved)++id;}
        if(id>=8||channels_[id].reserved||samples<64||samples>65472||(samples&63)||(format!=0&&format!=0x10)){r.stop("Unsupported audio channel reservation");return;}
        channels_[id]={true,samples,format,0x8000,0x8000,0};if(reset_)reset_(id);c.set_gpr(2,id);
    });
    rt.register_hle("sceAudio",0x6FC46853u,[this](auto &r,auto &c){if(c.gpr[4]>=8||!channels_[c.gpr[4]].reserved){r.stop("Unknown audio release");return;}channels_[c.gpr[4]]={};if(reset_)reset_(c.gpr[4]);c.set_gpr(2,0);});
    rt.register_hle("sceAudio",0xB011922Fu,[this,&hle](auto &r,auto &c){if(c.gpr[4]>=8||!channels_[c.gpr[4]].reserved){r.stop("Unknown audio channel");return;}const auto &channel=channels_[c.gpr[4]];c.set_gpr(2,channel.busy_until<=hle.virtual_time()?0:static_cast<std::uint32_t>((channel.busy_until-hle.virtual_time())*44100/1000000));});
    for(auto nid:{0xCB2E439Eu,0x95FD0C2Du,0xB7E1D8E7u})rt.register_hle("sceAudio",nid,[this,nid](auto &r,auto &c){
        if(c.gpr[4]>=8||!channels_[c.gpr[4]].reserved){r.stop("Unknown audio channel configuration");return;}auto &channel=channels_[c.gpr[4]];
        if(nid==0xCB2E439Eu){if(c.gpr[5]<64||c.gpr[5]>65472||(c.gpr[5]&63)){r.stop("Invalid audio sample count");return;}channel.samples=c.gpr[5];}
        else if(nid==0x95FD0C2Du){if(c.gpr[5]!=0&&c.gpr[5]!=0x10){r.stop("Invalid audio format");return;}channel.format=c.gpr[5];}
        else {if(c.gpr[5]>0x8000||c.gpr[6]>0x8000){r.stop("Invalid audio volume");return;}channel.left=c.gpr[5];channel.right=c.gpr[6];}c.set_gpr(2,0);
    });
    for(auto nid:{0x136CAF51u,0x13F592BCu,0xE2D56B2Du})rt.register_hle("sceAudio",nid,[this,&hle,nid](auto &r,auto &c){
        if(c.gpr[4]>=8||!channels_[c.gpr[4]].reserved){r.stop("Unknown audio output channel");return;}auto &channel=channels_[c.gpr[4]];
        const auto buffer=nid==0x136CAF51u?c.gpr[6]:c.gpr[7];const auto left=c.gpr[5],right=nid==0x136CAF51u?left:c.gpr[6];
        if(left>0x8000||right>0x8000){r.stop("Invalid audio output volume");return;}
        const auto now=hle.virtual_time(),start=std::max(now,channel.busy_until);
        if(nid==0xE2D56B2Du&&start>now){c.set_gpr(2,0x80260002);return;}
        if(buffer){
            const auto bytes=channel.samples*(channel.format?2:4);if(!r.memory().contains(buffer,bytes)){r.stop("Invalid PCM output buffer");return;}
            if(output_) {
                std::vector<std::int16_t> stereo(channel.samples*2);
                for(unsigned i=0;i<channel.samples;++i) {
                    const auto a=buffer+i*(channel.format?2:4);
                    const auto l=static_cast<std::int16_t>(r.memory().load16(a));
                    const auto rr=channel.format?l:static_cast<std::int16_t>(r.memory().load16(a+2));
                    stereo[i*2]=static_cast<std::int16_t>(std::int32_t(l)*static_cast<std::int32_t>(left)/0x8000);
                    stereo[i*2+1]=static_cast<std::int16_t>(std::int32_t(rr)*static_cast<std::int32_t>(right)/0x8000);
                }
                output_(c.gpr[4],stereo);
            }
            if(const auto dir=std::getenv("FF3_AUDIO_DIR")){std::filesystem::create_directories(dir);std::vector<std::uint8_t> data(bytes);r.memory().copy_out(buffer,data);std::ofstream out(std::filesystem::path(dir)/("channel_"+std::to_string(c.gpr[4])+".s16le"),std::ios::binary|std::ios::app);out.write(reinterpret_cast<const char*>(data.data()),data.size());}
            channel.busy_until=start+(static_cast<std::uint64_t>(channel.samples)*1000000+44099)/44100;
        }
        if(start>now)hle.delay(r,c,start-now,buffer?channel.samples:0);else c.set_gpr(2,buffer?channel.samples:0);
    });
}
}
