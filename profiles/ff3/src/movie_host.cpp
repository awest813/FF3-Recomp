// SPDX-License-Identifier: MIT
// PSP player ABI: rust-psp sys/psmf.rs; decoding uses the shared VCS decoder.
#include "movie_host.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
namespace ff3 {
void MovieHost::install(psprecomp::Runtime &rt,BootstrapHle &hle) {
    rt.register_hle("sceMpeg",0x682A619B,[this](auto &,auto &c){initialized_=true;c.set_gpr(2,0);});
    rt.register_hle("sceMpeg",0x874624D6,[this](auto &,auto &c){initialized_=false;c.set_gpr(2,0);});
    const auto get=[this](auto &c)->Player*{auto i=players_.find(c.gpr[4]);if(i==players_.end()){c.set_gpr(2,0x80616001);return nullptr;}return &i->second;};
    rt.register_hle("scePsmfPlayer",0x235D8787,[this](auto &r,auto &c){
        if(!r.memory().contains(c.gpr[4],4)||!r.memory().contains(c.gpr[5],12)){c.set_gpr(2,0x80616005);return;}
        if(players_.contains(c.gpr[4])){c.set_gpr(2,0x80616002);return;}
        players_.try_emplace(c.gpr[4]);r.memory().store32(c.gpr[4],c.gpr[4]);c.set_gpr(2,0);
    });
    rt.register_hle("scePsmfPlayer",0x1E57A8E7,[get](auto &r,auto &c){auto p=get(c);if(!p)return;
        if(c.gpr[5]==0&&c.gpr[6]<=1)p->loop=c.gpr[6];
        else if(c.gpr[5]==1&&c.gpr[6]<=3)p->format=c.gpr[6];
        else {r.stop("Unsupported PSMF player configuration");return;}c.set_gpr(2,0);
    });
    for(auto nid:{0x58B83577u,0x3D6D25A9u}) rt.register_hle("scePsmfPlayer",nid,[this,get](auto &r,auto &c){auto p=get(c);if(!p)return;
        auto name=r.memory().read_c_string(c.gpr[5],1024);const std::string prefix="disc0:/";
        if(!name.starts_with(prefix)){r.stop("Unsupported PSMF device");return;}
        auto root=std::filesystem::weakly_canonical(disc_);auto path=std::filesystem::weakly_canonical(root/name.substr(prefix.size()));
        auto rel=path.lexically_relative(root);if(rel.empty()||*rel.begin()==".."){r.stop("PSMF path escapes disc");return;}
        std::array<unsigned char,2048> h{};std::ifstream in(path,std::ios::binary);in.read(reinterpret_cast<char*>(h.data()),h.size());
        if(!in||std::memcmp(h.data(),"PSMF",4)){c.set_gpr(2,0x80616005);return;}
        const auto be=[&](unsigned a){return (std::uint32_t(h[a])<<24)|(std::uint32_t(h[a+1])<<16)|(h[a+2]<<8)|h[a+3];};
        p->width=h[0x8E]*16;p->height=h[0x8F]*16;p->duration=be(0x5C)-be(0x56);
        if(!p->width||!p->height||p->width>480||p->height>272){r.stop("Unsupported PSMF dimensions");return;}
        p->path=path;p->rgba.resize(p->width*p->height*4);p->status=2;
        std::cerr<<"[movie] "<<name<<" "<<p->width<<"x"<<p->height<<" duration_ticks="<<p->duration<<"\n";c.set_gpr(2,0);
    });
    rt.register_hle("scePsmfPlayer",0x95A84EE5,[get,&hle](auto &r,auto &c){auto p=get(c);if(!p)return;
        if(p->status!=2||!r.memory().contains(c.gpr[5],24)){c.set_gpr(2,0x80616002);return;}
        if(c.gpr[6]!=0||r.memory().load32(c.gpr[5]+16)!=0){r.stop("PSMF seek/play mode unsupported");return;}
        if(!p->video.open(p->path)||!p->audio.open(p->path)){r.stop("Cannot decode PSMF audio/video");return;}
        p->frame=0;p->ready=false;p->next_frame=hle.virtual_time();p->status=4;c.set_gpr(2,0);
    });
    rt.register_hle("scePsmfPlayer",0xA0B8CA55,[get,&hle](auto &r,auto &c){auto p=get(c);if(!p)return;
        if(p->status==4&&!p->play_mode&&hle.virtual_time()>=p->next_frame){
            auto got=p->video.read(p->rgba);if(got==p->rgba.size()){p->ready=true;++p->frame;p->next_frame+=33367;}
            else if(got){r.stop("Incomplete decoded PSMF frame");return;}
            else if(!p->loop){p->video.close();p->audio.close();if(!p->video.open(p->path)||!p->audio.open(p->path)){r.stop("PSMF loop reopen failed");return;}p->frame=0;p->ready=false;p->next_frame=hle.virtual_time();}
            else p->status=0x200;
        }c.set_gpr(2,0);
    });
    rt.register_hle("scePsmfPlayer",0x46F61F8B,[get](auto &r,auto &c){auto p=get(c);if(!p)return;
        if(p->status!=4&&p->status!=0x200){c.set_gpr(2,0x80616001);return;}
        if(!p->ready){c.set_gpr(2,0x8061600C);return;}
        auto &m=r.memory();auto d=c.gpr[5];if(!m.contains(d,12)){c.set_gpr(2,0x80616005);return;}
        auto stride=m.load32(d),out=m.load32(d+4),bpp=p->format==3?4u:2u;
        if(stride<p->width||stride>2048||!m.contains(out,stride*p->height*bpp)){r.stop("Invalid PSMF output buffer");return;}
        if(p->format==3) {
            for(unsigned y=0;y<p->height;++y)m.copy_in(out+y*stride*4,
                std::span<const std::uint8_t>(p->rgba).subspan(y*p->width*4,p->width*4));
        } else for(unsigned y=0;y<p->height;++y)for(unsigned x=0;x<p->width;++x){auto px=&p->rgba[4*(y*p->width+x)];auto a=out+(y*stride+x)*bpp;
            if(p->format==0)m.store16(a,(px[0]>>3)|((px[1]>>2)<<5)|((px[2]>>3)<<11));
            else if(p->format==1)m.store16(a,(px[0]>>3)|((px[1]>>3)<<5)|((px[2]>>3)<<10)|0x8000);
            else m.store16(a,(px[0]>>4)|((px[1]>>4)<<4)|((px[2]>>4)<<8)|0xF000);
        // A 30 fps movie is requested into alternating 60 Hz display buffers.
        // Repeated reads, pause and EOF must still copy the current frame.
        // PSP hardware autotests also observe repeated reads with the same PTS.
        }m.store32(d+8,(p->frame-1)*3003);c.set_gpr(2,0);
        if(std::getenv("FF3_MOVIE_TRACE") && (p->frame<=3 || p->frame%300==0))
            std::cerr<<"[movie-frame] index="<<p->frame<<" output="<<std::hex<<out<<std::dec
                     <<" stride="<<stride<<" format="<<p->format<<"\n";
    });
    rt.register_hle("scePsmfPlayer",0x3EA82A4B,[get](auto &,auto &c){if(get(c))c.set_gpr(2,8192);});
    rt.register_hle("scePsmfPlayer",0xB9848A74,[get](auto &r,auto &c){auto p=get(c);if(!p)return;
        if(p->status!=4||p->play_mode){c.set_gpr(2,0x8061600C);return;}
        if(!r.memory().contains(c.gpr[5],8192)){c.set_gpr(2,0x80616005);return;}
        std::array<std::uint8_t,8192> pcm{};auto got=p->audio.read(pcm);if(!got){c.set_gpr(2,0x8061600C);return;}
        r.memory().copy_in(c.gpr[5],pcm);c.set_gpr(2,0);
    });
    rt.register_hle("scePsmfPlayer",0xF8EF08A6,[get](auto &,auto &c){if(auto p=get(c))c.set_gpr(2,p->status);});
    rt.register_hle("scePsmfPlayer",0xDF089680,[get](auto &r,auto &c){auto p=get(c);if(!p)return;auto d=c.gpr[5];
        if(!r.memory().contains(d,20)){c.set_gpr(2,0x80616005);return;}
        for(auto [off,val]:{std::pair{0u,p->duration},std::pair{4u,1u},std::pair{8u,1u},std::pair{12u,0u},std::pair{16u,1u}})r.memory().store32(d+off,val);
        c.set_gpr(2,0);
    });
    rt.register_hle("scePsmfPlayer",0xA3D81169,[get](auto &r,auto &c){auto p=get(c);if(!p)return;if(c.gpr[5]!=0&&c.gpr[5]!=3){r.stop("Unsupported PSMF playback mode");return;}p->play_mode=c.gpr[5];c.set_gpr(2,0);});
    for(auto nid:{0x1078C008u,0xE792CD94u,0x9B71A274u})rt.register_hle("scePsmfPlayer",nid,[this,get,nid](auto &,auto &c){auto p=get(c);if(!p)return;p->video.close();p->audio.close();p->ready=false;p->status=nid==0x1078C008?2:1;if(nid==0x9B71A274)players_.erase(c.gpr[4]);c.set_gpr(2,0);});
}
}
