#include "bootstrap_hle.hpp"
namespace ff3 {
bool BootstrapHle::dispatch_callback(psprecomp::AllegrexContext &ctx,bool resume_import) {
    if (callback_frames_.contains(current_) || interrupt_frame_) return false;
    for (auto &[uid,callback] : callbacks_) {
        if (callback.owner!=current_ || !callback.pending) continue;
        auto caller=ctx;
        if (!resume_import) {caller.pc=ctx.gpr[31];caller.set_gpr(2,1);}
        callback_frames_.emplace(current_,CallbackFrame{uid,caller});
        ctx.gpr[4]=callback.pending;ctx.gpr[5]=callback.argument;ctx.gpr[6]=callback.common;
        callback.pending=0;ctx.gpr[31]=return_trampoline+4;ctx.pc=callback.entry;return true;
    }
    return false;
}
void BootstrapHle::install_callbacks(psprecomp::Runtime &runtime) {
    runtime.register_hle("ThreadManForUser", 0xE81CAF8Fu, [this](auto &rt, auto &ctx) {
        if (!current_ || !rt.has_function(ctx.gpr[5]) || callbacks_.size()>=256 || !rt.memory().contains(ctx.gpr[4],32)) {
            rt.stop("Bootstrap CreateCallback: invalid name/entry/owner or callback limit"); return;
        }
        const auto uid=next_uid_++;
        callbacks_.emplace(uid,Callback{rt.memory().read_c_string(ctx.gpr[4],31),ctx.gpr[5],ctx.gpr[6],0,0,current_});
        ctx.set_gpr(2,static_cast<std::uint32_t>(uid));
    });
    runtime.register_hle("ThreadManForUser", 0xEDBA5844u, [this](auto &rt, auto &ctx) {
        const auto uid=static_cast<std::int32_t>(ctx.gpr[4]);
        if (!callbacks_.erase(uid)) { rt.stop("Bootstrap DeleteCallback: unknown UID"); return; }
        if (exit_callback_==uid) exit_callback_.reset();
        ctx.set_gpr(2,0);
    });
    runtime.register_hle("LoadExecForUser", 0x4AC57943u, [this](auto &rt, auto &ctx) {
        const auto uid=static_cast<std::int32_t>(ctx.gpr[4]);
        if (!callbacks_.contains(uid)) { rt.stop("Bootstrap RegisterExitCallback: unknown UID"); return; }
        exit_callback_=uid; ctx.set_gpr(2,0);
    });
    runtime.register_hle("scePower",0x04B7766Eu,[this](auto &rt,auto &ctx) {
        auto slot=ctx.gpr[4]; const auto uid=static_cast<std::int32_t>(ctx.gpr[5]);
        if (slot==UINT32_MAX) { slot=0; while (power_callbacks_.contains(slot) && slot<16) ++slot; }
        const auto cb=callbacks_.find(uid);
        if (slot>=16 || power_callbacks_.contains(slot) || cb==callbacks_.end()) { rt.stop("Invalid power callback registration"); return; }
        power_callbacks_.emplace(slot,uid);
        ++cb->second.pending; cb->second.argument=0x1080u|100u; // Virtual PSP: AC, battery present/full.
        ctx.set_gpr(2,slot);
    });
    runtime.register_hle("ThreadManForUser", 0xC11BA8C4u, [this](auto &rt, auto &ctx) {
        const auto it=callbacks_.find(static_cast<std::int32_t>(ctx.gpr[4]));
        if (it==callbacks_.end() || it->second.pending==UINT32_MAX) { rt.stop("Bootstrap NotifyCallback: invalid UID/count"); return; }
        ++it->second.pending; it->second.argument=ctx.gpr[5]; ctx.set_gpr(2,0);
    });
    runtime.register_hle("ThreadManForUser", 0x349D6D6Cu, [this](auto &, auto &ctx) {
        ctx.set_gpr(2,0); dispatch_callback(ctx,false);
    });
    runtime.register_hle("ff3Bootstrap",1,[this](auto &rt,auto &ctx) {
        const auto frame=callback_frames_.find(current_);
        if (frame==callback_frames_.end()) { rt.stop("Bootstrap callback return without frame"); return; }
        if (ctx.gpr[2]) {
            callbacks_.erase(frame->second.uid);
            if (exit_callback_==frame->second.uid) exit_callback_.reset();
        }
        ctx=frame->second.caller; callback_frames_.erase(frame);
    });
    runtime.register_function(return_trampoline+4,[](auto &rt,auto &ctx) { rt.invoke_import("ff3Bootstrap",1,ctx); },"ff3_callback_return");
}
}
