#include "bootstrap_hle.hpp"
#include <algorithm>
namespace ff3 {
void BootstrapHle::advance(psprecomp::Runtime &rt,psprecomp::AllegrexContext &ctx,std::uint64_t us,bool at_import) {
    virtual_time_us_+=us; bool woken=false;
    for (auto &[uid,thread] : threads_) if (thread.state==State::waiting && thread.wait==Wait::delay && thread.wake_time<=virtual_time_us_) {
        thread.state=State::ready; thread.wait=Wait::none; thread.ready_order=++ready_order_; woken=true;
    }
    if (woken && current_) schedule(rt,ctx,false,at_import);
    if (hardware_hook_ && !rt.stopped() && !interrupt_frame_ && interrupts_enabled_) hardware_hook_(rt,ctx,false);
}
void BootstrapHle::delay(psprecomp::Runtime &rt,psprecomp::AllegrexContext &ctx,std::uint64_t us,std::uint32_t result) {
    if(!current_||!interrupts_enabled_||interrupt_frame_){rt.stop("Delay outside schedulable context");return;}
    auto &thread=threads_.at(current_);thread.wait=Wait::delay;thread.wake_time=virtual_time_us_+std::max<std::uint64_t>(1,us);block(rt,ctx,result);
}
void BootstrapHle::block(psprecomp::Runtime &rt,psprecomp::AllegrexContext &ctx,std::uint32_t result) {
    auto &thread=threads_.at(current_); thread.context=ctx; thread.context.pc=ctx.gpr[31]; thread.context.gpr[2]=result;
    thread.state=State::waiting; schedule(rt,ctx,true);
}
bool BootstrapHle::interrupt(psprecomp::AllegrexContext &ctx,std::uint32_t entry,std::uint32_t sub,std::uint32_t common) {
    if (!interrupts_enabled_ || interrupt_frame_ || !current_) return false;
    interrupt_frame_=ctx;
    ctx.pc=entry; ctx.gpr[4]=sub; ctx.gpr[5]=common; ctx.gpr[31]=0x0800000C;
    return true;
}
void BootstrapHle::install_synchronization(psprecomp::Runtime &runtime) {
    for(auto nid:{0x278C0DF5u,0x840E8133u})runtime.register_hle("ThreadManForUser",nid,[this,nid](auto &rt,auto &ctx){
        if(nid==0x840E8133u&&dispatch_callback(ctx,true))return;
        const auto id=static_cast<std::int32_t>(ctx.gpr[4]);auto it=threads_.find(id);
        if(it==threads_.end()){ctx.set_gpr(2,0x80020198);return;}
        if(id==current_||ctx.gpr[5]){rt.stop("Unsupported thread-end wait");return;}
        if(it->second.state==State::dormant){ctx.set_gpr(2,0);return;}
        auto &t=threads_.at(current_);t.wait=Wait::thread_end;t.wait_id=id;block(rt,ctx);
    });
    runtime.register_hle("ThreadManForUser",0x9FA03CD3,[this](auto &rt,auto &ctx){
        auto it=threads_.find(static_cast<std::int32_t>(ctx.gpr[4]));if(it==threads_.end()){ctx.set_gpr(2,0x80020198);return;}
        if(it->second.state!=State::dormant){ctx.set_gpr(2,0x800201A4);return;}
        if(it->second.stack_top)memory_.release(it->second.stack_bottom);threads_.erase(it);ctx.set_gpr(2,0);
    });
    runtime.register_hle("ff3Bootstrap",2,[this](auto &rt,auto &ctx){
        if (!interrupt_frame_) {rt.stop("Interrupt return without caller");return;}
        ctx=*interrupt_frame_; interrupt_frame_.reset();
        schedule(rt,ctx,threads_.at(current_).state!=State::running,false);
    });
    runtime.register_function(0x0800000C,[](auto &rt,auto &ctx){rt.invoke_import("ff3Bootstrap",2,ctx);},"ff3_interrupt_return");
    runtime.register_hle("ThreadManForUser",0x19CFF145u,[this](auto &rt,auto &ctx){
        const auto p=ctx.gpr[4],attr=ctx.gpr[6],count=ctx.gpr[7];
        if(!rt.memory().contains(p,32) || (attr&~0x300u) || ctx.gpr[8] || count>INT32_MAX || (count>1 && !(attr&0x200)) || mutexes_.contains(p)){rt.stop("Unsupported lightweight mutex options");return;}
        const auto id=next_uid_++;mutexes_.emplace(p,id);rt.memory().zero(p,32);rt.memory().store32(p,count);rt.memory().store32(p+4,count?current_:0);rt.memory().store32(p+8,attr);rt.memory().store32(p+16,id);ctx.set_gpr(2,0);
    });
    for(auto nid:{0xBEA46419u,0xDC692EE3u})runtime.register_hle("Kernel_Library",nid,[this,nid](auto &rt,auto &ctx){
        const auto p=ctx.gpr[4],count=ctx.gpr[5];if(!mutexes_.contains(p) || !count || count>INT32_MAX || (nid==0xBEA46419u && ctx.gpr[6])){rt.stop("Unsupported lightweight mutex wait");return;}
        const auto held=rt.memory().load32(p),owner=rt.memory().load32(p+4),attr=rt.memory().load32(p+8);
        if(!held || (owner==static_cast<std::uint32_t>(current_) && (attr&0x200))) {
            if(count>INT32_MAX-held || (count>1 && !(attr&0x200))){rt.stop("Invalid lightweight mutex lock count");return;}
            rt.memory().store32(p,held+count);rt.memory().store32(p+4,current_);ctx.set_gpr(2,0);return;
        }
        if(nid==0xDC692EE3u){rt.stop("Contended lightweight mutex try-lock");return;}
        auto &thread=threads_.at(current_);thread.wait=Wait::mutex;thread.wait_id=p;thread.wait_count=count;thread.ready_order=++ready_order_;rt.memory().store32(p+12,rt.memory().load32(p+12)+1);block(rt,ctx);
    });
    runtime.register_hle("Kernel_Library",0x15B6446Bu,[this](auto &rt,auto &ctx){
        const auto p=ctx.gpr[4],count=ctx.gpr[5];if(!mutexes_.contains(p)||!count||rt.memory().load32(p+4)!=static_cast<std::uint32_t>(current_)||count>rt.memory().load32(p)){rt.stop("Invalid lightweight mutex unlock");return;}
        const auto left=rt.memory().load32(p)-count;rt.memory().store32(p,left);
        if(!left){
            rt.memory().store32(p+4,0);auto selected=threads_.end();const auto attr=rt.memory().load32(p+8);
            for(auto it=threads_.begin();it!=threads_.end();++it)if(it->second.state==State::waiting && it->second.wait==Wait::mutex && it->second.wait_id==p){
                if(selected==threads_.end() || ((attr&0x100) && it->second.priority<selected->second.priority) || ((!(attr&0x100)||it->second.priority==selected->second.priority)&&it->second.ready_order<selected->second.ready_order))selected=it;
            }
            if(selected!=threads_.end()){auto &t=selected->second;rt.memory().store32(p,t.wait_count);rt.memory().store32(p+4,selected->first);rt.memory().store32(p+12,rt.memory().load32(p+12)-1);t.state=State::ready;t.wait=Wait::none;t.ready_order=++ready_order_;}
        }
        ctx.set_gpr(2,0);schedule(rt,ctx,false);
    });
    runtime.register_hle("ThreadManForUser",0x60107536u,[this](auto &rt,auto &ctx){
        const auto p=ctx.gpr[4];if(!mutexes_.erase(p)){rt.stop("Unknown lightweight mutex");return;}
        for(auto &[uid,t]:threads_)if(t.state==State::waiting&&t.wait==Wait::mutex&&t.wait_id==p){t.state=State::ready;t.wait=Wait::none;t.context.gpr[2]=0x800201B5;t.ready_order=++ready_order_;}
        rt.memory().zero(p,32);rt.memory().store32(p+16,UINT32_MAX);ctx.set_gpr(2,0);schedule(rt,ctx,false);
    });
    for (const auto nid : {0xCEADEB47u,0x68DA9E36u}) runtime.register_hle("ThreadManForUser",nid,[this,nid](auto &rt,auto &ctx) {
        if ((nid==0x68DA9E36u || nid==0x82826F70u) && dispatch_callback(ctx,true)) return;
        if (!current_ || !interrupts_enabled_) { rt.stop("DelayThread outside schedulable context"); return; }
        auto &thread=threads_.at(current_); thread.wait=Wait::delay; thread.wake_time=virtual_time_us_+std::max(1u,ctx.gpr[4]); block(rt,ctx);
    });
    for (const auto nid : {0x9ACE131Eu,0x82826F70u}) runtime.register_hle("ThreadManForUser",nid,[this,nid](auto &rt,auto &ctx) {
        if ((nid==0x68DA9E36u || nid==0x82826F70u) && dispatch_callback(ctx,true)) return;
        if (!current_ || !interrupts_enabled_) { rt.stop("SleepThread outside schedulable context"); return; }
        auto &thread=threads_.at(current_);
        if (thread.wake_count) { --thread.wake_count; ctx.set_gpr(2,0); return; }
        thread.wait=Wait::sleep; block(rt,ctx);
    });
    runtime.register_hle("ThreadManForUser",0xD59EAD2Fu,[this](auto &rt,auto &ctx) {
        const auto it=threads_.find(static_cast<std::int32_t>(ctx.gpr[4]));
        if (it==threads_.end()) { rt.stop("WakeupThread unknown UID"); return; }
        auto &thread=it->second;
        if (thread.state==State::waiting && thread.wait==Wait::sleep) { thread.state=State::ready; thread.wait=Wait::none; thread.ready_order=++ready_order_; }
        else ++thread.wake_count;
        ctx.set_gpr(2,0); schedule(rt,ctx,false);
    });
    runtime.register_hle("ThreadManForUser",0x369ED59Du,[this](auto &,auto &ctx) { ctx.set_gpr(2,static_cast<std::uint32_t>(virtual_time_us_)); });
    runtime.register_hle("ThreadManForUser",0x82BC5777u,[this](auto &,auto &ctx) { ctx.set_gpr(2,static_cast<std::uint32_t>(virtual_time_us_)); ctx.set_gpr(3,static_cast<std::uint32_t>(virtual_time_us_>>32)); });
    runtime.register_hle("ThreadManForUser",0xDB738F35u,[this](auto &rt,auto &ctx) { if (!rt.memory().contains(ctx.gpr[4],8)) { rt.stop("Invalid clock result"); return; } rt.memory().store32(ctx.gpr[4],static_cast<std::uint32_t>(virtual_time_us_)); rt.memory().store32(ctx.gpr[4]+4,static_cast<std::uint32_t>(virtual_time_us_>>32)); ctx.set_gpr(2,0); });
    runtime.register_hle("ThreadManForUser",0xD6DA4BA1u,[this](auto &rt,auto &ctx) {
        if (ctx.gpr[8] || (ctx.gpr[5]&~0x100u) || ctx.gpr[6]>ctx.gpr[7] || !ctx.gpr[7]) { rt.stop("Unsupported semaphore creation"); return; }
        const auto uid=next_uid_++; semaphores_.emplace(uid,Semaphore{ctx.gpr[6],ctx.gpr[7],ctx.gpr[5]}); ctx.set_gpr(2,uid);
    });
    const auto wait=[this](auto &rt,auto &ctx,bool poll) {
        const auto it=semaphores_.find(static_cast<std::int32_t>(ctx.gpr[4]));
        if (it==semaphores_.end() || !ctx.gpr[5] || ctx.gpr[5]>it->second.maximum || (!poll && ctx.gpr[6])) { rt.stop("Unsupported semaphore wait"); return; }
        if (it->second.count>=ctx.gpr[5]) { it->second.count-=ctx.gpr[5]; ctx.set_gpr(2,0); return; }
        if (poll) { ctx.set_gpr(2,0x800201ADu); return; } // SCE_KERNEL_ERROR_SEMA_ZERO.
        if (!interrupts_enabled_) { rt.stop("Semaphore wait with interrupts disabled"); return; }
        auto &thread=threads_.at(current_); thread.wait=Wait::semaphore; thread.wait_id=ctx.gpr[4]; thread.wait_count=ctx.gpr[5]; thread.ready_order=++ready_order_; block(rt,ctx);
    };
    for (auto nid : {0x4E3A1105u,0x6D212BACu}) runtime.register_hle("ThreadManForUser",nid,[this,wait,nid](auto &rt,auto &ctx) { if(nid==0x6D212BACu && dispatch_callback(ctx,true))return;wait(rt,ctx,false); });
    runtime.register_hle("ThreadManForUser",0x58B1F937u,[wait](auto &rt,auto &ctx) { wait(rt,ctx,true); });
    runtime.register_hle("ThreadManForUser",0x3F53E640u,[this](auto &rt,auto &ctx) {
        const auto it=semaphores_.find(static_cast<std::int32_t>(ctx.gpr[4]));
        if (it==semaphores_.end() || !ctx.gpr[5] || ctx.gpr[5]>it->second.maximum-it->second.count) { rt.stop("Invalid semaphore signal"); return; }
        auto &sema=it->second; sema.count+=ctx.gpr[5];
        while (true) {
            auto selected=threads_.end();
            for (auto t=threads_.begin();t!=threads_.end();++t) if (t->second.state==State::waiting && t->second.wait==Wait::semaphore && t->second.wait_id==ctx.gpr[4]) {
                if (selected==threads_.end() || ((sema.attributes&0x100u) && t->second.priority<selected->second.priority) ||
                    ((!(sema.attributes&0x100u) || t->second.priority==selected->second.priority) && t->second.ready_order<selected->second.ready_order)) selected=t;
            }
            if (selected==threads_.end() || selected->second.wait_count>sema.count) break;
            sema.count-=selected->second.wait_count; selected->second.state=State::ready; selected->second.wait=Wait::none; selected->second.ready_order=++ready_order_;
        }
        ctx.set_gpr(2,0); schedule(rt,ctx,false);
    });
    runtime.register_hle("ThreadManForUser",0x28B6489Cu,[this](auto &rt,auto &ctx) {
        if (!semaphores_.erase(static_cast<std::int32_t>(ctx.gpr[4]))) { rt.stop("DeleteSema unknown UID"); return; }
        for (auto &[uid,t]:threads_) if (t.state==State::waiting && t.wait==Wait::semaphore && t.wait_id==ctx.gpr[4]) { t.state=State::ready; t.wait=Wait::none; t.context.gpr[2]=0x800201B5u; t.ready_order=++ready_order_; }
        ctx.set_gpr(2,0); schedule(rt,ctx,false);
    });
    runtime.register_hle("ThreadManForUser",0x55C20A00u,[this](auto &rt,auto &ctx) {
        if (ctx.gpr[7] || (ctx.gpr[5]&~0x300u)) {rt.stop("Unsupported event flag options");return;}
        const auto uid=next_uid_++; events_.emplace(uid,EventFlag{ctx.gpr[6],ctx.gpr[5]});ctx.set_gpr(2,uid);
    });
    const auto event_matches=[](std::uint32_t current,std::uint32_t bits,std::uint32_t mode) {return (mode&1)?(current&bits)!=0:(current&bits)==bits;};
    const auto consume=[](EventFlag &event,std::uint32_t bits,std::uint32_t mode) {if(mode&0x10)event.bits=0;else if(mode&0x20)event.bits&=~bits;};
    const auto event_wait=[this,event_matches,consume](auto &rt,auto &ctx,bool poll) {
        const auto it=events_.find(static_cast<std::int32_t>(ctx.gpr[4]));const auto bits=ctx.gpr[5],mode=ctx.gpr[6],out=ctx.gpr[7];
        if(it==events_.end() || !bits || (mode&~0x31u) || ((mode&0x30)==0x30) || (out&&!rt.memory().contains(out,4))) {rt.stop("Invalid event wait");return;}
        if(event_matches(it->second.bits,bits,mode)) {if(out)rt.memory().store32(out,it->second.bits);consume(it->second,bits,mode);ctx.set_gpr(2,0);return;}
        if(poll) {ctx.set_gpr(2,0x800201AFu);return;}
        if(ctx.gpr[8] || !interrupts_enabled_) {rt.stop("Event timeout/masked wait not implemented");return;}
        auto &thread=threads_.at(current_);thread.wait=Wait::event;thread.wait_id=ctx.gpr[4];thread.wait_count=bits;thread.wait_mode=mode;thread.wait_output=out;thread.ready_order=++ready_order_;block(rt,ctx);
    };
    for(auto nid:{0x402FCF22u,0x328C546Au})runtime.register_hle("ThreadManForUser",nid,[this,event_wait,nid](auto &rt,auto &ctx){if(nid==0x328C546Au && dispatch_callback(ctx,true))return;event_wait(rt,ctx,false);});
    runtime.register_hle("ThreadManForUser",0x30FD48F0u,[event_wait](auto &rt,auto &ctx){event_wait(rt,ctx,true);});
    runtime.register_hle("ThreadManForUser",0x1FB15A32u,[this,event_matches,consume](auto &rt,auto &ctx){
        const auto it=events_.find(static_cast<std::int32_t>(ctx.gpr[4]));if(it==events_.end()){rt.stop("SetEventFlag unknown UID");return;}
        auto &event=it->second;event.bits|=ctx.gpr[5];
        std::vector<std::int32_t> waiters;
        for(const auto &[uid,t]:threads_)if(t.state==State::waiting&&t.wait==Wait::event&&t.wait_id==ctx.gpr[4])waiters.push_back(uid);
        std::sort(waiters.begin(),waiters.end(),[&](auto a,auto b){const auto &x=threads_.at(a),&y=threads_.at(b);if((event.attributes&0x100)&&x.priority!=y.priority)return x.priority<y.priority;return x.ready_order<y.ready_order;});
        for(auto uid:waiters){auto &t=threads_.at(uid);if(!event_matches(event.bits,t.wait_count,t.wait_mode))continue;if(t.wait_output)rt.memory().store32(t.wait_output,event.bits);consume(event,t.wait_count,t.wait_mode);t.state=State::ready;t.wait=Wait::none;t.ready_order=++ready_order_;}
        ctx.set_gpr(2,0);schedule(rt,ctx,false);
    });
    runtime.register_hle("ThreadManForUser",0x812346E4u,[this](auto &rt,auto &ctx){auto i=events_.find(static_cast<std::int32_t>(ctx.gpr[4]));if(i==events_.end()){rt.stop("ClearEventFlag unknown UID");return;}i->second.bits&=ctx.gpr[5];ctx.set_gpr(2,0);});
    runtime.register_hle("ThreadManForUser",0xEF9E4C70u,[this](auto &rt,auto &ctx){if(!events_.erase(static_cast<std::int32_t>(ctx.gpr[4]))){rt.stop("DeleteEventFlag unknown UID");return;}for(auto &[uid,t]:threads_)if(t.state==State::waiting&&t.wait==Wait::event&&t.wait_id==ctx.gpr[4]){t.state=State::ready;t.wait=Wait::none;t.context.gpr[2]=0x800201B5u;t.ready_order=++ready_order_;}ctx.set_gpr(2,0);schedule(rt,ctx,false);});

}
}
