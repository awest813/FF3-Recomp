// Optional asset-dependent check. Not registered with asset-free CTest suites.
#include "movie_host.hpp"
#include <iostream>
#include <stdexcept>
using namespace psprecomp;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv) {
 try {
    if(argc!=2)throw std::runtime_error("Usage: ff3_movie_check <extracted-disc-root>");
    Runtime r;ff3::BootstrapHle h;ff3::MovieHost movie(argv[1]);movie.install(r,h);
    auto &m=r.memory();auto &c=r.cpu();
    constexpr unsigned handle=0x08800000,config=handle+0x100,path=handle+0x200,play=handle+0x400,data=handle+0x500,audio=handle+0x1000;
    constexpr unsigned front=0x04000000,back=0x04088000;
    const std::string name="disc0:/PSP_GAME/USRDIR/FILES/MOVIE/opening.pmf";
    m.zero(handle,0x4000);for(unsigned i=0;i<name.size();++i)m.store8(path+i,name[i]);
    auto call=[&](unsigned nid,std::initializer_list<unsigned> args){unsigned reg=4;for(auto arg:args)c.gpr[reg++]=arg;r.invoke_import("scePsmfPlayer",nid,c);require(!r.stopped(),r.stop_reason().c_str());return c.gpr[2];};
    require(call(0x235D8787,{handle,config})==0,"create player");
    require(call(0x58B83577,{handle,path})==0,"open PMF");
    require(call(0x95A84EE5,{handle,play,0})==0,"start PMF");
    std::vector<std::uint8_t> first(512*272*4),second(first.size());
    unsigned last_pts=0,frames=0,nonblack=0;std::uint64_t audio_samples=0,audio_nonzero=0,next_audio=0;
    for(unsigned tick=0;tick<20000;++tick) {
        call(0xA0B8CA55,{handle});
        const auto status=call(0xF8EF08A6,{handle});
        m.store32(data,512);m.store32(data+4,front);
        require(call(0x46F61F8B,{handle,data})==0,"video read");
        const auto pts=m.load32(data+8);require(pts>=last_pts,"movie PTS moved backwards");
        m.copy_out(front,first);m.zero(back,first.size());m.store32(data+4,back);
        require(call(0x46F61F8B,{handle,data})==0&&m.load32(data+8)==pts,"repeat read lost current frame/PTS");
        m.copy_out(back,second);require(first==second,"alternating display buffer received different frame");
        for(unsigned i=0;i<480*4;i+=4)if(first[100*512*4+i]||first[100*512*4+i+1]||first[100*512*4+i+2]){++nonblack;break;}
        last_pts=pts;++frames;
        if(tick==30) {
            call(0xA3D81169,{handle,3});call(0xA0B8CA55,{handle});
            require(call(0x46F61F8B,{handle,data})==0&&m.load32(data+8)==pts,"paused frame not retained");
            require(call(0xB9848A74,{handle,audio})==0x8061600C,"paused soundtrack advanced");
            call(0xA3D81169,{handle,0});
        }
        while(next_audio<=h.virtual_time()&&status==4) {
            const auto result=call(0xB9848A74,{handle,audio});
            if(!result) {
                audio_samples+=2048;
                for(unsigned i=0;i<8192;i+=2)if(m.load16(audio+i))++audio_nonzero;
            } else require(result==0x8061600C,"audio decode failure");
            next_audio=audio_samples*1000000/44100;
            if(result)break;
        }
        if(status==0x200)break;
        h.advance(r,c,33367);
    }
    require(call(0xF8EF08A6,{handle})==0x200,"movie did not reach EOF");
    require(nonblack>100&&audio_nonzero>10000,"movie pixels or soundtrack remained blank");
    std::cout<<"Movie checked: reads="<<frames<<" last_pts="<<last_pts<<" nonblack_frames="<<nonblack
             <<" audio_frames="<<audio_samples<<" audio_nonzero_samples="<<audio_nonzero<<"\n";
    return 0;
 } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
