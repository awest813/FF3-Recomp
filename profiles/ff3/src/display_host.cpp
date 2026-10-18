#include "display_host.hpp"
#include "psprecomp/common.hpp"
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
namespace ff3 {
DisplayHost::DisplayHost() {
    vcs::reset_ge_transform_state(transform_);
    for (std::uint32_t i=0;i<256;++i) commands_[i]=i<<24;
    const auto dir=std::getenv("FF3_FRAME_DIR"); frame_dir_=dir ? dir : "profiles/ff3/analysis/menu/frames";
}
void DisplayHost::callbacks(psprecomp::AllegrexContext &ctx) {
    const auto owner=psprecomp::runtime_thread_uid();
    if (pending_.empty() || callers_.contains(owner)) return;
    auto caller=ctx; caller.pc=ctx.gpr[31]; callers_.emplace(owner,caller);
    const auto callback=pending_.front(); pending_.pop_front();
    ctx.pc=callback.entry; ctx.gpr[4]=callback.id; ctx.gpr[5]=callback.common; ctx.gpr[31]=0x08000008;
}
void DisplayHost::frame(psprecomp::Runtime &rt) {
    ++frames_;
    if (framebuffer_.address) {
        auto rgb=vcs::decode_framebuffer_rgb(rt.memory(),framebuffer_);
        if (presenter_) presenter_(rt,rgb);
        const auto every_text=std::getenv("FF3_FRAME_EVERY");
        const auto every=every_text?std::max(1ul,std::strtoul(every_text,nullptr,10)):1ul;
        const auto start_text=std::getenv("FF3_FRAME_START");
        const auto start=start_text?std::strtoull(start_text,nullptr,10):0;
        if (capture_enabled_ && frames_>=start && rgb!=last_frame_ && (frames_%every==0 || last_frame_.empty())) {
            std::filesystem::create_directories(frame_dir_);
            std::ostringstream name; name<<"frame_"<<std::setfill('0')<<std::setw(6)<<frames_<<".ppm";
            vcs::write_framebuffer_ppm(frame_dir_/name.str(),framebuffer_,rgb);
            last_frame_=std::move(rgb); std::cout<<"[frame] "<<frames_<<" "<<(frame_dir_/name.str()).string()<<'\n';
        }
    } else if (presenter_) presenter_(rt,{});
    const auto limit=std::getenv("FF3_MAX_FRAMES");
    if (limit && frames_>=std::strtoull(limit,nullptr,10)) rt.stop("FF3 diagnostic frame limit");
}
void DisplayHost::run_lists(psprecomp::Runtime &rt) {
    const auto relative=[this](std::uint32_t data) { return ((((commands_[0x10]&0x00FF0000u)<<8)|data)+offset_)&0x0FFFFFFFu; };
    for (auto &[id,list]:lists_) {
        if (list.done) continue;
        std::uint32_t steps=0;
        while (!list.done && !rt.stopped() && (!list.stall || list.pc!=list.stall)) {
            if (++steps>1000000 || !rt.memory().contains(list.pc,4)) { rt.stop("Invalid/unbounded GE list"); return; }
            const auto pc=list.pc, op=rt.memory().load32(pc), cmd=op>>24, data=op&0xFFFFFF;
            if(std::getenv("FF3_GE_COMMANDS") && id<5) std::cerr<<"[ge-cmd] list="<<id<<" pc="<<psprecomp::hex32(pc)<<" op="<<psprecomp::hex32(op)<<"\n";
            list.pc+=4; commands_[cmd]=op; ++revision_;
            vcs::update_ge_transform_state(transform_,cmd,data);
            switch (cmd) {
            case 0x01: vertex_=relative(data); break;
            case 0x02: index_=relative(data); break;
            case 0x04: {
                vcs::GeRenderStats stats; std::string error;
                if (!vcs::render_ge_primitive(rt.memory(),commands_,transform_,vertex_,index_,data,stats,error,1,revision_,revision_,revision_)) { rt.stop("GE primitive: "+error); return; }
                if (stats.unsupported_primitives) { rt.stop("GE unsupported primitive format"); return; }
                if(std::getenv("FF3_GE_TRACE")) std::cerr<<"[ge] pc="<<psprecomp::hex32(pc)<<" prim="<<psprecomp::hex32(data)<<" fb="<<psprecomp::hex32(commands_[0x9C])<<" stride="<<psprecomp::hex32(commands_[0x9D])<<" vertices="<<stats.decoded_vertices<<" pixels="<<stats.pixels_written<<" xy="<<stats.screen_min_x<<","<<stats.screen_min_y<<".."<<stats.screen_max_x<<","<<stats.screen_max_y<<"\n";
                vertex_=stats.next_vertex_address; index_=stats.next_index_address; break;
            }
            case 0x05: case 0x06: rt.stop("GE patch tessellation not implemented"); return;
            case 0x07: { vcs::GeBoundingBoxResult result; std::string error; if (!vcs::test_ge_bounding_box(rt.memory(),commands_,transform_,vertex_,index_,data,result,error)) { rt.stop(error); return; } vertex_=result.next_vertex_address; index_=result.next_index_address; bbox_visible_=result.visible; break; }
            case 0x08: list.pc=relative(data); break;
            case 0x09: if (!bbox_visible_) list.pc=relative(data); break;
            case 0x0A: if (list.calls.size()>=32) { rt.stop("GE call stack overflow"); return; } list.calls.push_back(list.pc); list.pc=relative(data); break;
            case 0x0B: if (list.calls.empty()) { rt.stop("GE return without call"); return; } list.pc=list.calls.back(); list.calls.pop_back(); break;
            case 0x0C: if(list.signal){list.signal=0;}else list.done=true; break;
            case 0x0E: {
                const auto mode=data>>16; list.signal=mode;
                if (mode!=1 && mode!=2 && mode!=3) { rt.stop("Unsupported GE signal behavior "+psprecomp::hex32(data)); return; }
                const auto cb=callbacks_.find(list.callback);
                if (cb!=callbacks_.end() && cb->second.signal) pending_.push_back({cb->second.signal,data&65535,cb->second.signal_arg});
                break;
            }
            case 0x0F: { const auto cb=callbacks_.find(list.callback); if (cb!=callbacks_.end() && cb->second.finish) pending_.push_back({cb->second.finish,data&65535,cb->second.finish_arg}); break; }
            case 0x13: offset_=data<<8; break;
            case 0x14: offset_=pc; break;
            case 0xEA: { // GE block transfer; preserve overlap with a temporary row buffer.
                const auto source=(commands_[0xB2]&0xFFFFFF)|((commands_[0xB3]&0xFF0000)<<8);
                const auto target=(commands_[0xB4]&0xFFFFFF)|((commands_[0xB5]&0xFF0000)<<8);
                const auto sx=commands_[0xEB]&1023,sy=(commands_[0xEB]>>10)&1023;
                const auto dx=commands_[0xEC]&1023,dy=(commands_[0xEC]>>10)&1023;
                const auto w=(commands_[0xEE]&1023)+1,h=((commands_[0xEE]>>10)&1023)+1,bpp=(data&1)?4u:2u;
                std::vector<std::uint8_t> bytes(w*h*bpp);
                for (std::uint32_t y=0;y<h;++y) rt.memory().copy_out(source+((sy+y)*(commands_[0xB3]&65535)+sx)*bpp,std::span(bytes).subspan(y*w*bpp,w*bpp));
                for (std::uint32_t y=0;y<h;++y) rt.memory().copy_in(target+((dy+y)*(commands_[0xB5]&65535)+dx)*bpp,std::span(bytes).subspan(y*w*bpp,w*bpp));
                break;
            }
            default: break; // GE render state is retained in commands_ and consumed by the rasterizer.
            }
        }
        if (!list.done) break; // Queue order is preserved across a stall.
    }
}
void DisplayHost::install(psprecomp::Runtime &rt, BootstrapHle &hle) {
    hle.set_hardware_hook([this,&hle](auto &r,auto &c,bool idle){
        if (hle.virtual_time()<next_vblank_) return false;
        // Keep the 60 Hz phase; scheduling lateness must not accumulate each tick.
        next_vblank_+=((hle.virtual_time()-next_vblank_)/16667+1)*16667;
        run_lists(r); frame(r);
        for (const auto &[sub,handler]:interrupts_) if (handler.enabled) {
            if (hle.interrupt(c,handler.entry,sub,handler.common)) return true;
        }
        (void)idle; return false;
    });
    rt.register_hle("InterruptManager",0xCA04A2B9u,[this](auto &r,auto &c){
        if(c.gpr[4]!=30 || c.gpr[5]>=16 || !r.has_function(c.gpr[6])){r.stop("Unsupported subinterrupt registration");return;}
        if(interrupts_.contains(c.gpr[5])){c.set_gpr(2,0x80020067);return;}
        interrupts_.emplace(c.gpr[5],SubInterrupt{c.gpr[6],c.gpr[7],false});c.set_gpr(2,0);
    });
    for(auto nid:{0xFB8E22ECu,0x8A389411u,0xD61E6961u})rt.register_hle("InterruptManager",nid,[this,nid](auto &r,auto &c){
        const auto it=interrupts_.find(c.gpr[5]);if(c.gpr[4]!=30||it==interrupts_.end()){r.stop("Unknown subinterrupt");return;}
        if(nid==0xD61E6961u)interrupts_.erase(it);else it->second.enabled=nid==0xFB8E22ECu;c.set_gpr(2,0);
    });
    rt.register_hle("sceGe_user",0xE47E40E4u,[](auto &,auto &c){c.set_gpr(2,0x04000000);});
    rt.register_hle("sceGe_user",0x1F6752ADu,[](auto &,auto &c){c.set_gpr(2,2*1024*1024);});
    rt.register_hle("sceGe_user",0xB77905EAu,[this](auto &,auto &c){const auto old=translation_;translation_=c.gpr[4];c.set_gpr(2,old);});
    rt.register_hle("sceGe_user",0xA4FC06A4u,[this](auto &r,auto &c){if (!r.memory().contains(c.gpr[4],16)){r.stop("Invalid GE callback data");return;}const auto p=c.gpr[4],id=next_callback_++; callbacks_.emplace(id,Callback{r.memory().load32(p),r.memory().load32(p+4),r.memory().load32(p+8),r.memory().load32(p+12)});c.set_gpr(2,id);});
    rt.register_hle("sceGe_user",0x05DB22CEu,[this](auto &,auto &c){c.set_gpr(2,callbacks_.erase(c.gpr[4])?0:0x80000100);});
    const auto enqueue=[this](auto &r,auto &c){
        if (c.gpr[7]) {
            const auto p=c.gpr[7];
            if (!r.memory().contains(p,8)) {r.stop("Invalid GE list options");return;}
            const auto size=r.memory().load32(p),context=r.memory().load32(p+4);
            if (size!=8 && size!=16) {r.stop("Unsupported GE list option size "+std::to_string(size));return;}
            if (context) {r.stop("GE saved context requested at "+psprecomp::hex32(context));return;}
            if (size==16 && (!r.memory().contains(p,16) || r.memory().load32(p+8)>32)) {r.stop("Unsupported GE list stack count");return;}
        }
        if (lists_.size()>64) for(auto i=lists_.begin();i!=lists_.end();) { if(i->second.done)i=lists_.erase(i);else ++i; }
        const auto id=next_list_++; lists_.emplace(id,List{c.gpr[4]&0x0FFFFFFFu,c.gpr[5]&0x0FFFFFFFu,c.gpr[6],false,{}});
        run_lists(r); c.set_gpr(2,id); if(!r.stopped())callbacks(c);
    };
    rt.register_hle("sceGe_user",0xAB49E76Au,enqueue);
    rt.register_hle("sceGe_user",0x1C0D95A6u,enqueue);
    rt.register_hle("sceGe_user",0xE0D68148u,[this](auto &r,auto &c){const auto it=lists_.find(c.gpr[4]);if(it==lists_.end()){r.stop("Unknown GE list");return;}it->second.stall=c.gpr[5]&0x0FFFFFFFu;run_lists(r);c.set_gpr(2,0);if(!r.stopped())callbacks(c);});
    rt.register_hle("sceGe_user",0x03444EB4u,[this](auto &r,auto &c){run_lists(r);const auto it=lists_.find(c.gpr[4]);if(it==lists_.end()){r.stop("Unknown GE list sync");return;}if(!c.gpr[5]&&!it->second.done){r.stop("GE blocking sync on stalled list");return;}c.set_gpr(2,it->second.done?0:3);if(!r.stopped())callbacks(c);});
    rt.register_hle("sceGe_user",0xB287BD61u,[this](auto &r,auto &c){run_lists(r);bool done=true;for(const auto &[id,l]:lists_)done=done&&l.done;if(!c.gpr[4]&&!done){r.stop("GE draw sync on stalled queue");return;}c.set_gpr(2,done?0:3);if(!r.stopped())callbacks(c);});
    rt.register_hle("sceGe_user",0xDC93CFEFu,[this](auto &r,auto &c){if(c.gpr[4]>=256){r.stop("Invalid GE command index");return;}c.set_gpr(2,commands_[c.gpr[4]]&0xFFFFFF);});
    rt.register_hle("sceDisplay",0x0E20F177u,[this](auto &r,auto &c){if(c.gpr[4]||c.gpr[5]!=480||c.gpr[6]!=272){r.stop("Unsupported display mode");return;}c.set_gpr(2,0);});
    rt.register_hle("sceDisplay",0x289D82FEu,[this](auto &r,auto &c){if(c.gpr[6]>3 || (c.gpr[4]&&c.gpr[5]<480)){r.stop("Invalid display framebuffer");return;}framebuffer_.address=c.gpr[4];framebuffer_.stride=c.gpr[5];framebuffer_.pixel_format=c.gpr[6];c.set_gpr(2,0);});
    rt.register_hle("sceDisplay",0x9C6EAAD7u,[this](auto &,auto &c){c.set_gpr(2,static_cast<std::uint32_t>(frames_));});
    for(auto nid:{0x36CDFADEu,0x8EB9EC49u,0x984C27E7u,0x46F186C3u})rt.register_hle("sceDisplay",nid,[this](auto &r,auto &c){if(r.stopped())return;c.gpr[4]=16667;r.invoke_import("ThreadManForUser",0xCEADEB47u,c);});
    rt.register_hle("ff3Graphics",0,[this](auto &r,auto &c){const auto it=callers_.find(psprecomp::runtime_thread_uid());if(it==callers_.end()){r.stop("GE callback return without caller");return;}c=it->second;callers_.erase(it);callbacks(c);});
    rt.register_function(0x08000008,[](auto &r,auto &c){r.invoke_import("ff3Graphics",0,c);},"ff3_ge_callback_return");
}
}
