#include "utility_host.hpp"
#include "psprecomp/common.hpp"
#include <cstdlib>
#include <iostream>
namespace ff3 {
void UtilityHost::install(psprecomp::Runtime &runtime) {
    install_keyboard(runtime);
    runtime.register_hle("sceUtility",0x2AD8E239u,[this](auto &rt,auto &ctx) {
        const auto p=ctx.gpr[4];if(!rt.memory().contains(p,580)||rt.memory().load32(p)<580){ctx.set_gpr(2,0x80110004);return;}
        if(status_||keyboard_status_){ctx.set_gpr(2,0x80110001);return;}
        message_=p;status_=1;updates_=0;
        std::cerr<<"[dialog] mode="<<rt.memory().load32(p+52)<<" error="<<psprecomp::hex32(rt.memory().load32(p+56))<<" options="<<psprecomp::hex32(rt.memory().load32(p+572))<<" text="<<rt.memory().read_c_string(p+60,512)<<"\n";
        ctx.set_gpr(2,0);
    });
    runtime.register_hle("sceUtility",0x9A1C91D7,[this](auto &,auto &c){c.set_gpr(2,status_);if(status_==1)status_=2;else if(status_==4)status_=0;});
    runtime.register_hle("sceUtility",0x95FC253B,[this](auto &r,auto &c){
        if(status_!=2){c.set_gpr(2,0x80110001);return;}
        ++updates_;
        const auto response=std::getenv("FF3_DIALOG_RESPONSE");
        if((response||dialog_handler_)&&updates_>=2){
            const auto button=response?std::strtoul(response,nullptr,10):dialog_handler_(r.memory().read_c_string(message_+60,512),r.memory().load32(message_+572));
            if(button<1||button>3){r.stop("FF3_DIALOG_RESPONSE must be 1 (confirm), 2 (no), or 3 (back)");return;}
            r.memory().store32(message_+576,button);r.memory().store32(message_+28,0);status_=3;
            std::cerr<<"[dialog] response="<<button<<"\n";
        }
        c.set_gpr(2,0);
    });
    runtime.register_hle("sceUtility",0x67AF3428,[this](auto &,auto &c){if(status_!=3){c.set_gpr(2,0x80110001);return;}status_=4;c.set_gpr(2,0);});
    runtime.register_hle("sceUtility",0x4928BD96,[this](auto &r,auto &c){if(status_!=2){c.set_gpr(2,0x80110001);return;}r.memory().store32(message_+576,3);status_=3;c.set_gpr(2,0);});
}
void UtilityHost::install_keyboard(psprecomp::Runtime &runtime) {
    // PSPSDK SceUtilityOskParams / SceUtilityOskData use 32-bit pointers and UTF-16.
    runtime.register_hle("sceUtility",0xF6269B82u,[this](auto &r,auto &c){
        auto &m=r.memory();const auto p=c.gpr[4];
        if (keyboard_status_||status_) {c.set_gpr(2,0x80110001);return;}
        if (!m.contains(p,64)||m.load32(p)<64||m.load32(p+48)!=1) {c.set_gpr(2,0x80110004);return;}
        const auto data=m.load32(p+52);
        if (!m.contains(data,52)) {c.set_gpr(2,0x80110004);return;}
        const auto capacity=m.load32(data+36),out=m.load32(data+40),limit=m.load32(data+48);
        if (!capacity||capacity>4096||limit>4096||!m.contains(out,capacity*2u)) {c.set_gpr(2,0x80110004);return;}
        const auto read_text=[&](std::uint32_t address)->std::optional<std::u16string>{
            std::u16string text;if (!address) return text;
            for (unsigned i=0;i<4096;++i) {
                if (!m.contains(address,2)) return std::nullopt;
                const auto ch=m.load16(address);if (!ch) return text;
                text.push_back(static_cast<char16_t>(ch));address+=2;
            }
            return std::nullopt;
        };
        const auto description=read_text(m.load32(data+28)),initial=read_text(m.load32(data+32));
        if (!description||!initial) {c.set_gpr(2,0x80110004);return;}
        keyboard_params_=p;keyboard_data_=data;keyboard_output_=out;
        keyboard_limit_=std::min(capacity-1,limit?limit:capacity-1);
        keyboard_description_=*description;keyboard_initial_=*initial;keyboard_status_=1;
        m.store32(p+56,1);c.set_gpr(2,0);
    });
    runtime.register_hle("sceUtility",0xF3F76017u,[this](auto &r,auto &c){
        c.set_gpr(2,keyboard_status_);
        if (keyboard_status_==1) {keyboard_status_=2;r.memory().store32(keyboard_params_+56,3);}
        else if (keyboard_status_==4) {keyboard_status_=0;r.memory().store32(keyboard_params_+56,0);}
    });
    runtime.register_hle("sceUtility",0x4B85C861u,[this](auto &r,auto &c){
        if (keyboard_status_!=2) {c.set_gpr(2,0x80110001);return;}
        if (!keyboard_handler_) {r.stop("OSK requires an interactive keyboard handler");return;}
        const auto answer=keyboard_handler_(keyboard_description_,keyboard_initial_,keyboard_limit_);
        auto text=answer?*answer:keyboard_initial_;
        if (text.size()>keyboard_limit_) text.resize(keyboard_limit_);
        if (!text.empty() && text.back()>=0xD800 && text.back()<=0xDBFF) text.pop_back();
        auto &m=r.memory();
        for (unsigned i=0;i<text.size();++i) m.store16(keyboard_output_+i*2,text[i]);
        m.store16(keyboard_output_+text.size()*2,0);
        m.store32(keyboard_data_+44,!answer?1:text==keyboard_initial_?0:2);
        m.store32(keyboard_params_+28,0);m.store32(keyboard_params_+56,4);
        keyboard_status_=3;c.set_gpr(2,0);
    });
    runtime.register_hle("sceUtility",0x3DFAEBA9u,[this](auto &r,auto &c){
        if (keyboard_status_!=3) {c.set_gpr(2,0x80110001);return;}
        keyboard_status_=4;r.memory().store32(keyboard_params_+56,5);c.set_gpr(2,0);
    });
}
}
