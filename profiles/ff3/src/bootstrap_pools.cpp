#include "bootstrap_hle.hpp"
#include <algorithm>
namespace ff3 {
void BootstrapHle::install_pools(psprecomp::Runtime &runtime) {
    runtime.register_hle("ThreadManForUser", 0xC07BB470u,
        [this](auto &rt, auto &ctx) {
            const auto size=ctx.gpr[7], count=ctx.gpr[8];
            if (ctx.gpr[5]!=2 || ctx.gpr[6] || ctx.gpr[9] || !size || size>UINT32_MAX-15u ||
                !count || count>4096 || pools_.size()>=256) {
                rt.stop("Bootstrap CreateFpl: unsupported partition/attributes/options or pool size"); return;
            }
            const auto stride=(size+15u)&~15u;
            const auto total=static_cast<std::uint64_t>(stride)*count;
            if (total>memory_.max_free()) { rt.stop("Bootstrap CreateFpl: pool exceeds free arena"); return; }
            std::string name;
            for (unsigned i=0; i<32; ++i) {
                const auto address=static_cast<std::uint64_t>(ctx.gpr[4])+i;
                if (address>UINT32_MAX || !rt.memory().contains(static_cast<std::uint32_t>(address))) {
                    rt.stop("Bootstrap CreateFpl: invalid name pointer"); return;
                }
                const auto ch=rt.memory().load8(static_cast<std::uint32_t>(address));
                if (!ch) break;
                if (i==31) { rt.stop("Bootstrap CreateFpl: name exceeds diagnostic limit"); return; }
                name.push_back(static_cast<char>(ch));
            }
            const auto address=memory_.allocate(static_cast<std::uint32_t>(total), 256, false);
            if (!address) { rt.stop("Bootstrap CreateFpl: arena exhausted"); return; }
            const auto uid=next_uid_++;
            pools_.emplace(uid, FixedPool{name,*address,stride,std::vector<bool>(count,false)});
            ctx.set_gpr(2,static_cast<std::uint32_t>(uid));
        });
    const auto allocate=[this](auto &rt, auto &ctx, bool try_only) {
        const auto pool=pools_.find(static_cast<std::int32_t>(ctx.gpr[4]));
        const auto out=ctx.gpr[5];
        if (pool==pools_.end() || (out&3u) || !rt.memory().contains(out,4) || (!try_only && ctx.gpr[6])) {
            rt.stop("Bootstrap AllocateFpl: invalid UID/output or unsupported timeout"); return;
        }
        auto &record=pool->second;
        const auto block=std::find(record.allocated.begin(),record.allocated.end(),false);
        if (block==record.allocated.end()) {
            rt.stop("Bootstrap AllocateFpl: exhausted pool; waits/error codes not implemented"); return;
        }
        const auto index=static_cast<std::uint32_t>(block-record.allocated.begin());
        rt.memory().store32(out,record.address+index*record.stride);
        *block=true; ctx.set_gpr(2,0);
    };
    runtime.register_hle("ThreadManForUser", 0xD979E9BFu,
        [allocate](auto &rt, auto &ctx) { allocate(rt,ctx,false); });
    runtime.register_hle("ThreadManForUser", 0x623AE665u,
        [allocate](auto &rt, auto &ctx) { allocate(rt,ctx,true); });
    runtime.register_hle("ThreadManForUser", 0xF6414A71u,
        [this](auto &rt, auto &ctx) {
            const auto pool=pools_.find(static_cast<std::int32_t>(ctx.gpr[4]));
            if (pool==pools_.end()) { rt.stop("Bootstrap FreeFpl: unknown pool UID"); return; }
            auto &record=pool->second; const auto pointer=ctx.gpr[5];
            if (pointer<record.address || (pointer-record.address)%record.stride ||
                (pointer-record.address)/record.stride>=record.allocated.size()) {
                rt.stop("Bootstrap FreeFpl: pointer is not a pool block"); return;
            }
            const auto index=(pointer-record.address)/record.stride;
            if (!record.allocated[index]) { rt.stop("Bootstrap FreeFpl: block already free"); return; }
            record.allocated[index]=false; ctx.set_gpr(2,0);
        });
    runtime.register_hle("ThreadManForUser", 0xED1410E0u,
        [this](auto &rt, auto &ctx) {
            const auto pool=pools_.find(static_cast<std::int32_t>(ctx.gpr[4]));
            if (pool==pools_.end()) { rt.stop("Bootstrap DeleteFpl: unknown pool UID"); return; }
            memory_.release(pool->second.address); pools_.erase(pool); ctx.set_gpr(2,0);
        });
}
}
