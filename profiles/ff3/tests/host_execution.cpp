#include "display_host.hpp"
#include "file_host.hpp"
#include "utility_host.hpp"
#include "audio_host.hpp"
#include "pcm_mixer.hpp"
#include "savedata_host.hpp"
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psprecomp;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
void ge_signal_continues() {
    Runtime r;ff3::BootstrapHle h;ff3::DisplayHost d;d.install(r,h);
    auto &m=r.memory();constexpr unsigned list=0x08800000,vertices=0x08801000;
    const unsigned commands[]={0x9C000000,0x9D000200,0xD2000003,0xD4000000,0xD5043DDF,
        0xD3000101,0x0E020001,0x0C000000,0x10080000,0x01801000,0x1280019C,0x04060002,0x0F000000,0x0C000000};
    for(unsigned i=0;i<std::size(commands);++i)m.store32(list+4*i,commands[i]);
    for(unsigned i=0;i<2;++i){m.store32(vertices+16*i,0xFF0000FF);m.store32(vertices+16*i+4,std::bit_cast<unsigned>(i?16.f:0.f));m.store32(vertices+16*i+8,std::bit_cast<unsigned>(i?16.f:0.f));m.store32(vertices+16*i+12,0);}
    auto &c=r.cpu();c.gpr[4]=list;c.gpr[5]=0;c.gpr[6]=0;c.gpr[7]=0;
    r.invoke_import("sceGe_user",0xAB49E76A,c);
    check(!r.stopped(),r.stop_reason().c_str());
    check(m.load32(0x04000000)==0x000000FF,"GE SIGNAL/END discarded following draw");
    check(m.load32(0x04000000+16*4)==0,"GE rectangle exceeded its bounds");
    auto id=c.gpr[2];c.gpr[4]=id;c.gpr[5]=1;r.invoke_import("sceGe_user",0x03444EB4,c);
    check(c.gpr[2]==0,"GE FINISH/END did not finish list");
}
// Adjacent triangles must blend every panel pixel exactly once, in either winding.
void ge_ui_panel_edges() {
    for(bool reverse:{false,true}) for(float offset:{0.f,0.5f}) {
        Runtime r;ff3::BootstrapHle h;ff3::DisplayHost d;d.install(r,h);
        auto &m=r.memory();constexpr unsigned list=0x08800000,vertices=0x08801000;
        const unsigned commands[]={0x9C000000,0x9D000200,0xD2000003,0xD4000000,0xD5043DDF,
            0xD3000000,0x21000001,0xDF000032,0x10080000,0x01801000,0x1280019C,
            0x04030006,0x0F000000,0x0C000000};
        for(unsigned i=0;i<std::size(commands);++i)m.store32(list+4*i,commands[i]);
        const float xy[6][2]={{0,0},{8,0},{8,8},{0,0},{8,8},{0,8}};
        for(unsigned i=0;i<6;++i) {
            const unsigned j=reverse?(i/3*3+2-i%3):i;
            m.store32(vertices+16*i,0x800000FF);
            m.store32(vertices+16*i+4,std::bit_cast<unsigned>(xy[j][0]+offset));
            m.store32(vertices+16*i+8,std::bit_cast<unsigned>(xy[j][1]+offset));m.store32(vertices+16*i+12,0);
        }
        auto &c=r.cpu();c.gpr[4]=list;r.invoke_import("sceGe_user",0xAB49E76A,c);
        check(!r.stopped(),r.stop_reason().c_str());
        for(unsigned y=0;y<9;++y)for(unsigned x=0;x<9;++x)
            check((m.load32(0x04000000+4*(y*512+x))&255)==(x<8&&y<8?128u:0u),
                  "UI panel shared edge has duplicate blending or missing pixels");
    }
}
void ge_sprite_clip() {
    Runtime r;ff3::BootstrapHle h;ff3::DisplayHost d;d.install(r,h);
    auto &m=r.memory();constexpr unsigned list=0x08800000,vertices=0x08801000;
    const unsigned commands[]={0x9C000000,0x9D000200,0xD2000003,0xD4000000,0xD5000000,
        0xD3000000,0x10080000,0x01801000,0x1280019C,0x04060002,0x0F000000,0x0C000000};
    for(unsigned i=0;i<std::size(commands);++i)m.store32(list+4*i,commands[i]);
    for(unsigned i=0;i<2;++i){m.store32(vertices+16*i,0xFF0000FF);
        m.store32(vertices+16*i+4,std::bit_cast<unsigned>(i?8.f:0.f));
        m.store32(vertices+16*i+8,std::bit_cast<unsigned>(i?8.f:0.f));m.store32(vertices+16*i+12,0);}
    auto &c=r.cpu();c.gpr[4]=list;r.invoke_import("sceGe_user",0xAB49E76A,c);
    check(!r.stopped(),r.stop_reason().c_str());
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        check((m.load32(0x04000000+4*(y*512+x))&255)==(x==0&&y==0?255u:0u),
              "Sprite escaped a one-pixel scissor rectangle");
}
void ge_textured_sprite() {
    for(bool flipped:{false,true}) for(bool half_pixel:{false,true}) {
        Runtime r;ff3::BootstrapHle h;ff3::DisplayHost d;d.install(r,h);
        auto &m=r.memory();constexpr unsigned list=0x08800000,vertices=0x08801000,texture=0x08802000;
        const unsigned commands[]={0x9C000000,0x9D000200,0xD2000003,0xD4000000,0xD5043DDF,
            0xD3000000,0x1E000001,0xA0802000,0xA8080002,0xB8000001,0xC3000003,
            0xC9000103,0x21000001,0xDF000032,0x10080000,0x01801000,0x1280019F,
            0x04060002,0x0F000000,0x0C000000};
        for(unsigned i=0;i<std::size(commands);++i)m.store32(list+4*i,commands[i]);
        m.store32(texture,0xFF0000FF);m.store32(texture+4,half_pixel?0xFF00FF00:0x0000FF00);
        m.store32(0x04000000,0x00FF0000);m.store32(0x04000004,0x00FF0000);
        for(unsigned i=0;i<2;++i) {
            m.store32(vertices+24*i,std::bit_cast<unsigned>((bool(i)!=flipped)?2.f:0.f));
            m.store32(vertices+24*i+4,std::bit_cast<unsigned>(i?1.f:0.f));
            m.store32(vertices+24*i+8,0xFFFFFFFF);
            m.store32(vertices+24*i+12,std::bit_cast<unsigned>((i?2.f:0.f)-(half_pixel?0.5f:0.f)));
            m.store32(vertices+24*i+16,std::bit_cast<unsigned>(i?1.f:0.f));m.store32(vertices+24*i+20,0);
        }
        auto &c=r.cpu();c.gpr[4]=list;r.invoke_import("sceGe_user",0xAB49E76A,c);
        check(!r.stopped(),r.stop_reason().c_str());
        check((m.load32(0x04000000)&0xFFFFFF)==(half_pixel?0xFF00u:(flipped?0xFF0000u:0xFFu)),"Sprite first texel or alpha incorrect");
        check((m.load32(0x04000004)&0xFFFFFF)==(half_pixel?0xFF0000u:(flipped?0xFFu:0xFF0000u)),"Sprite flipped UV or transparency incorrect");
        check(m.load32(0x04000008)==0,"Sprite sampling escaped screen bounds");
    }
}
void files(const std::filesystem::path &root) {
    std::filesystem::create_directories(root/"disc");
    {std::ofstream f(root/"disc.iso",std::ios::binary);std::string sector(2048,'x');f<<sector<<"ABCDEFGH";}
    Runtime r;ff3::FileHost host(root/"disc");host.install(r);auto &c=r.cpu();auto &m=r.memory();
    constexpr unsigned path=0x08800000,out=0x08801000;
    const std::string name="disc0:/sce_lbn0x1_size0x4";for(unsigned i=0;i<=name.size();++i)m.store8(path+i,i<name.size()?name[i]:0);
    c.gpr[4]=path;c.gpr[5]=1;r.invoke_import("IoFileMgrForUser",0x109F50BC,c);auto fd=c.gpr[2];check(fd>=1000,"Raw-sector open failed");
    c.gpr[4]=fd;c.gpr[5]=out;c.gpr[6]=8;r.invoke_import("IoFileMgrForUser",0x6A638D83,c);
    check(c.gpr[2]==4&&m.load32(out)==0x44434241,"Raw-sector read escaped extent");
    c.gpr[4]=fd;c.gpr[5]=out;c.gpr[6]=8;r.invoke_import("IoFileMgrForUser",0x6A638D83,c);check(c.gpr[2]==0,"Raw-sector EOF failed");
}
void savedata(const std::filesystem::path &root) {
    const auto saves=root/"saves";
    constexpr unsigned p=0x08800000,buffer=0x08801000,info=0x08802000,entries=0x08803000;
    auto text=[](GuestMemory &m,unsigned address,const std::string &value){
        for(unsigned i=0;i<=value.size();++i)m.store8(address+i,i<value.size()?value[i]:0);
    };
    auto setup=[&](Runtime &r){
        ff3::install_savedata(r,saves);auto &m=r.memory();m.zero(p,0x600);m.store32(p,0x600);
        text(m,p+0x3c,"NPUH10125");text(m,p+0x4c,"DATA00");text(m,p+0x64,"PLAYDATA.BIN");
        m.store32(p+0x74,buffer);m.store32(p+0x78,32);
    };
    auto operation=[&](Runtime &r,unsigned mode){
        auto &c=r.cpu();auto &m=r.memory();m.store32(p+0x30,mode);c.gpr[4]=p;
        r.invoke_import("sceUtility",0x50C4CD57,c);check(c.gpr[2]==0,"Savedata init failed");
        r.invoke_import("sceUtility",0x8874DBE0,c);check(c.gpr[2]==1,"Savedata INIT missing");
        r.invoke_import("sceUtility",0x8874DBE0,c);check(c.gpr[2]==2,"Savedata VISIBLE missing");
        r.invoke_import("sceUtility",0xD4B95FFB,c);
        const auto result=m.load32(p+0x1c);
        r.invoke_import("sceUtility",0x8874DBE0,c);check(c.gpr[2]==3,"Savedata QUIT missing");
        r.invoke_import("sceUtility",0x9790B33C,c);check(c.gpr[2]==0,"Savedata shutdown failed");
        r.invoke_import("sceUtility",0x8874DBE0,c);check(c.gpr[2]==4,"Savedata FINISHED missing");
        r.invoke_import("sceUtility",0x8874DBE0,c);check(c.gpr[2]==0,"Savedata did not reset");
        check(!r.stopped(),"Savedata stopped runtime");return result;
    };
    {
        Runtime r;setup(r);auto &m=r.memory();
        m.store32(buffer,0x12345678);m.store32(p+0x7c,4);
        check(operation(r,13)==0,"Secure save failed");
        m.store32(buffer,0x87654321);check(operation(r,17)==0,"Secure overwrite failed");
        m.store32(p+0x7c,33);check(operation(r,17)!=0,"Oversize write accepted");
        m.store32(p+0x7c,4);m.store32(buffer,0xBADBAD00);
        m.store32(p+0x584,buffer);m.store32(p+0x588,1);m.store32(p+0x58c,2);
        check(operation(r,3)!=0,"Invalid icon descriptor accepted");
    }
    {
        Runtime r;setup(r);auto &m=r.memory();
        check(operation(r,15)==0 && m.load32(buffer)==0x87654321 && m.load32(p+0x7c)==4,
              "Save did not survive fresh runtime or a failed data/auxiliary overwrite");
        m.store32(p+0x78,2);m.store32(buffer,0xABCDEF01);
        check(operation(r,15)!=0 && m.load32(buffer)==0xABCDEF01,"Small load buffer was overwritten");
        m.store32(p+0x78,32);text(m,p+0x4c,"MISSING");
        check(operation(r,15)==0x80110329,"Missing save result incorrect");
        text(m,p+0x4c,"DATA00");m.store32(p+0x7c,0);
        check(operation(r,17)==0,"Empty save failed");
        check(std::filesystem::file_size(saves/"NPUH10125DATA00/PLAYDATA.BIN")==0,"Empty overwrite retained old bytes");
        check(operation(r,15)==0 && m.load32(p+0x7c)==0,"Empty load failed");
        std::filesystem::create_directories(saves/"NPUH10125");
        std::filesystem::create_directories(saves/"NPUH10125DATA01");
        std::filesystem::create_directories(saves/"NPUH10125OTHER");
        m.store32(p+0x5f4,info);m.store32(info,8);m.store32(info+8,entries);text(m,p+0x4c,"DATA*");
        check(operation(r,11)==0 && m.load32(info+4)==2,"Slot filter included system/unrelated directories");
        check(m.read_c_string(entries+52,20)=="DATA00" && m.read_c_string(entries+72+52,20)=="DATA01","Slot sorting incorrect");
        text(m,p+0x4c,"DATA?1");check(operation(r,11)==0 && m.load32(info+4)==1,"Slot '?' filter failed");
        text(m,p+0x4c,"../escape");check(operation(r,15)!=0,"Invalid save path accepted");
        text(m,p+0x4c,"");m.zero(entries,60);text(m,entries,"DATA02");text(m,entries+20,"DATA03");
        m.store32(p+0x60,entries);m.store32(p+0x7c,4);m.store32(buffer,0xFF330123);
        ff3::set_save_slot_handler([](const auto &slots,bool saving)->std::optional<std::size_t>{
            check(saving && slots.size()==2 && slots[1].name=="DATA03" && !slots[1].occupied,"Save picker options incorrect");return 1;
        });
        check(operation(r,5)==0 && m.read_c_string(p+0x4c,20)=="DATA03","LISTSAVE did not return selected name");
        check(!std::filesystem::exists(saves/"NPUH10125/PLAYDATA.BIN"),"LISTSAVE wrote into system directory");
        ff3::set_save_slot_handler([](const auto &,bool)->std::optional<std::size_t>{return std::nullopt;});
        m.store32(buffer,0xBADBAD00);check(operation(r,5)==0x80110301,"Canceled save did not cancel");
        ff3::set_save_slot_handler([](const auto &slots,bool saving)->std::optional<std::size_t>{
            check(!saving && slots.size()==1 && slots[0].name=="DATA03" && slots[0].occupied,"Load picker included empty slots");return 0;
        });
        text(m,p+0x4c,"");check(operation(r,4)==0 && m.load32(buffer)==0xFF330123,"LISTLOAD failed or canceled save changed data");
        ff3::set_save_slot_handler({});check(operation(r,5)==0x80110301,"Headless list save silently selected a slot");
        ff3::install_savedata(r,root/"not-a-directory");
        {std::ofstream f(root/"not-a-directory");f<<"occupied";}
        text(m,p+0x4c,"DATA00");m.store32(p+0x7c,4);
        check(operation(r,13)!=0,"Filesystem failure reported success");
    }
}
void live_dialog() {
    Runtime r;ff3::UtilityHost host;host.install(r);auto &c=r.cpu();auto &m=r.memory();
    constexpr unsigned data=0x08800000;
    m.zero(data,580);m.store32(data,580);m.store32(data+572,0x10);
    const std::string text="Keep this choice?";
    for(unsigned i=0;i<text.size();++i)m.store8(data+60+i,text[i]);
    unsigned calls=0;
    host.set_dialog_handler([&](const std::string &message,unsigned options){
        check(message==text&&options==0x10,"dialog text/options lost");++calls;return 2;
    });
    c.gpr[4]=data;r.invoke_import("sceUtility",0x2AD8E239,c);
    r.invoke_import("sceUtility",0x9A1C91D7,c);check(c.gpr[2]==1,"dialog init status");
    r.invoke_import("sceUtility",0x95FC253B,c);check(calls==0,"dialog shown before ready");
    r.invoke_import("sceUtility",0x95FC253B,c);
    check(calls==1&&m.load32(data+576)==2&&m.load32(data+28)==0,"dialog response not returned");
    r.invoke_import("sceUtility",0x9A1C91D7,c);check(c.gpr[2]==3,"dialog did not finish");
    r.invoke_import("sceUtility",0x67AF3428,c);
    r.invoke_import("sceUtility",0x9A1C91D7,c);check(c.gpr[2]==4,"dialog shutdown status");
    r.invoke_import("sceUtility",0x9A1C91D7,c);check(c.gpr[2]==0&&!r.stopped(),"dialog idle status");
}
void live_keyboard() {
    for (unsigned result=0;result<3;++result) {
        Runtime r;ff3::UtilityHost host;host.install(r);auto &c=r.cpu();auto &m=r.memory();
        constexpr unsigned p=0x08800000,data=p+0x100,description=p+0x200,initial=p+0x300,out=p+0x400;
        m.zero(p,0x500);m.store32(p,64);m.store32(p+48,1);m.store32(p+52,data);
        m.store32(data+28,description);m.store32(data+32,initial);m.store32(data+36,7);
        m.store32(data+40,out);m.store32(data+48,6);m.store16(out-2,0xCAFE);m.store16(out+14,0xBEEF);
        for(unsigned i=0;i<6;++i)m.store16(initial+2*i,u"Luneth"[i]);
        m.store16(description,u'N');
        host.set_keyboard_handler([&](const auto &desc,const auto &name,unsigned limit)->std::optional<std::u16string>{
            check(desc==u"N"&&name==u"Luneth"&&limit==6,"OSK parameters lost");
            if(result==1)return std::nullopt;
            return result==0?name:u"Arc";
        });
        c.gpr[4]=p;r.invoke_import("sceUtility",0xF6269B82,c);check(c.gpr[2]==0,"OSK init failed");
        r.invoke_import("sceUtility",0xF3F76017,c);check(c.gpr[2]==1&&m.load32(p+56)==3,"OSK visible state");
        r.invoke_import("sceUtility",0x4B85C861,c);
        check(!r.stopped()&&m.load32(data+44)==result,"OSK result lost");
        const auto expected=result==2?std::u16string(u"Arc"):std::u16string(u"Luneth");
        for(unsigned i=0;i<expected.size();++i)check(m.load16(out+i*2)==expected[i],"OSK text mismatch");
        check(m.load16(out+expected.size()*2)==0&&m.load16(out-2)==0xCAFE&&m.load16(out+14)==0xBEEF,"OSK output bounds");
        r.invoke_import("sceUtility",0xF3F76017,c);check(c.gpr[2]==3,"OSK quit state");
        r.invoke_import("sceUtility",0x3DFAEBA9,c);
        r.invoke_import("sceUtility",0xF3F76017,c);check(c.gpr[2]==4,"OSK finished state");
        r.invoke_import("sceUtility",0xF3F76017,c);check(c.gpr[2]==0,"OSK idle state");
        m.store32(data+36,UINT32_MAX);c.gpr[4]=p;r.invoke_import("sceUtility",0xF6269B82,c);
        check(c.gpr[2]==0x80110004,"OSK accepted overflowing capacity");
    }
}
void audio_output() {
    Runtime r;ff3::BootstrapHle h;ff3::AudioHost audio;audio.install(r,h);
    auto &c=r.cpu();auto &m=r.memory();constexpr unsigned pcm=0x08800000;
    std::vector<std::int16_t> received;unsigned resets=0;
    audio.set_output([&](unsigned channel,auto samples){check(channel==0,"audio channel lost");received.assign(samples.begin(),samples.end());},
                     [&](unsigned channel){check(channel==0,"audio reset channel");++resets;});
    c.gpr[4]=0;c.gpr[5]=64;c.gpr[6]=0;r.invoke_import("sceAudio",0x5EC81C55,c);
    for(unsigned i=0;i<64;++i){m.store16(pcm+i*4,0x8000);m.store16(pcm+i*4+2,0x7fff);}
    c.gpr[4]=0;c.gpr[5]=0x4000;c.gpr[6]=0x2000;c.gpr[7]=pcm;r.invoke_import("sceAudio",0xE2D56B2D,c);
    check(received.size()==128&&received[0]==-16384&&received[1]==8191,"stereo signed gain/panning");
    c.gpr[4]=0;r.invoke_import("sceAudio",0x6FC46853,c);check(resets==2,"audio release did not flush device channel");
    c.gpr[4]=0;c.gpr[5]=64;c.gpr[6]=0x10;r.invoke_import("sceAudio",0x5EC81C55,c);
    for(unsigned i=0;i<64;++i)m.store16(pcm+i*2,static_cast<std::uint16_t>(-10000));
    c.gpr[4]=0;c.gpr[5]=0x2000;c.gpr[6]=0x8000;c.gpr[7]=pcm;r.invoke_import("sceAudio",0xE2D56B2D,c);
    check(received[0]==-2500&&received[1]==-10000&&!r.stopped(),"mono expansion/panning");
}
void pcm_volume() {
    ff3::PcmMixer mixer;
    std::vector<std::int16_t> pcm(1024,20000),out(1024);
    mixer.volume(50);mixer.submit(0,pcm);mixer.render(out);
    check(out[0]<20000&&out[0]>10000&&out.back()==10000,"PCM volume ramp or final gain");
    for(unsigned i=2;i<out.size();i+=2)check(out[i]<=out[i-2]&&out[i]==out[i+1],"PCM ramp monotonic/stereo");
    mixer.volume(0);mixer.submit(0,pcm);mixer.render(out);
    check(out.back()==0,"PCM zero volume");
    mixer.volume(200);
    for(unsigned c=0;c<8;++c)mixer.submit(c,pcm);
    mixer.render(out);check(out.back()==32767,"PCM volume clamp and wide accumulator");
}
void pcm_mixer() {
    ff3::PcmMixer mixer;
    const std::int16_t a[]{30000,-30000,100,-100},b[]{10000,-10000};
    mixer.submit(0,a);mixer.submit(1,b);std::int16_t out[6];mixer.render(out);
    check(out[0]==32767&&out[1]==-32768&&out[2]==100&&out[3]==-100&&out[4]==0,"PCM mix/clipping/silence");
    mixer.submit(0,a);mixer.clear(0);mixer.render(out);check(out[0]==0,"PCM channel flush");
    mixer.submit(0,a);mixer.enable(false);mixer.submit(1,b);mixer.enable(true);mixer.render(out);
    check(out[0]==0&&mixer.stats().played==2,"muted PCM replayed after resume");
    std::vector<std::int16_t> large((ff3::PcmMixer::capacity+1)*2,12);mixer.submit(0,large);
    mixer.render(out);check(out[0]==12&&mixer.stats().dropped==1,"PCM bounded overflow policy");
}
int main(){auto root=std::filesystem::temp_directory_path()/("ff3-host-regressions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{ge_signal_continues();ge_ui_panel_edges();ge_sprite_clip();ge_textured_sprite();files(root);savedata(root);live_dialog();live_keyboard();audio_output();pcm_mixer();pcm_volume();std::filesystem::remove_all(root);std::cout<<"FF3 host tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
