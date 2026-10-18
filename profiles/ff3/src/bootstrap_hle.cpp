#include "bootstrap_hle.hpp"
#include <chrono>
#include <ctime>
#include "guest_printf.hpp"
#include <iostream>
#include "psprecomp/common.hpp"
#include <algorithm>
#include <array>
#include <vector>
#include <sstream>
#include <cstdlib>
namespace ff3 {
void BootstrapHle::install(psprecomp::Runtime &runtime) {
    auto volatile_mem_lock = [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
        constexpr std::uint32_t volatile_base = 0x08400000u;
        constexpr std::uint32_t volatile_size = 0x00400000u;
        if (ctx.gpr[4] != 0u) {
            ctx.set_gpr(2, 0x80000107u);
            return;
        }
        if (volatile_memory_locked) {
            ctx.set_gpr(2, 0x80000021u);
            return;
        }
        if (!rt.memory().contains(volatile_base, volatile_size) ||
            !rt.memory().contains(ctx.gpr[5], 4u) || !rt.memory().contains(ctx.gpr[6], 4u)) {
            ctx.set_gpr(2, 0x800200D3u);
            return;
        }
        rt.memory().store32(ctx.gpr[5], volatile_base);
        rt.memory().store32(ctx.gpr[6], volatile_size);
        rt.memory().zero(volatile_base, volatile_size);
        volatile_memory_locked = true;
        ctx.set_gpr(2,0);
    };
    runtime.register_hle("sceSuspendForUser", 0x3E0271D3u, volatile_mem_lock);
    runtime.register_hle("sceSuspendForUser", 0xA14F40B2u, volatile_mem_lock);
    runtime.register_hle("sceSuspendForUser", 0xA569E425u,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            if (ctx.gpr[4] != 0u) {
                ctx.set_gpr(2, 0x80000107u);
                return;
            }
            if (!volatile_memory_locked) {
                ctx.set_gpr(2, 0x800201AEu);
                return;
            }
            volatile_memory_locked = false;
            ctx.set_gpr(2,0);
        });
    runtime.register_hle("ThreadManForUser",0x94AA61EE,[this](auto &,auto &c){c.set_gpr(2,current_?threads_.at(current_).priority:32);});
    if(const auto input=std::getenv("FF3_INPUT_SCRIPT")){
        std::istringstream script(input);std::string event;
        while(std::getline(script,event,',')){
            auto colon=event.find(':');if(colon==std::string::npos)throw psprecomp::Error("FF3_INPUT_SCRIPT needs milliseconds:button_mask pairs");
            auto when=std::stoull(event.substr(0,colon),nullptr,0)*1000;
            auto mask=std::stoul(event.substr(colon+1),nullptr,0);
            if(mask>0xFFFFFF || (!input_script_.empty()&&when<input_script_.back().first))throw psprecomp::Error("Invalid controller event");
            input_script_.emplace_back(when,mask);
        }
    }
    install_pools(runtime);
    install_callbacks(runtime);
    install_synchronization(runtime);
    runtime.register_hle("sceImpose",0x36AA6E91u,[this](auto &,auto &ctx) { language_=ctx.gpr[4]; confirm_button_=ctx.gpr[5]; ctx.set_gpr(2,0); });
    runtime.register_hle("sceUtility",0xA5DA2406u,[](auto &rt,auto &ctx) {
        const auto id=ctx.gpr[4],out=ctx.gpr[5];
        if (id<2 || id>9 || !rt.memory().contains(out,4)) { rt.stop("Invalid system parameter query"); return; }
        const std::uint32_t values[]{0,0,0,1,0,0,0,0,1,1}; // English, cross confirm, UTC, 24-hour clock.
        rt.memory().store32(out,values[id]); ctx.set_gpr(2,0);
    });
    runtime.register_hle("sceCtrl",0xA7144800u,[this](auto &,auto &ctx) { idle_cancel_x_=ctx.gpr[4]; idle_cancel_y_=ctx.gpr[5]; ctx.set_gpr(2,0); });
    runtime.register_hle("sceCtrl",0x1F4011E6u,[this](auto &,auto &ctx) { const auto old=controller_mode_; controller_mode_=ctx.gpr[4]; ctx.set_gpr(2,old); });
    runtime.register_hle("sceCtrl",0x6A2774F3u,[this](auto &,auto &ctx) { const auto old=controller_cycle_; controller_cycle_=ctx.gpr[4]; ctx.set_gpr(2,old); });
    for (auto nid : {0x1F803938u,0x3A622550u}) runtime.register_hle("sceCtrl",nid,[this](auto &rt,auto &ctx) {
        const auto count=ctx.gpr[5], address=ctx.gpr[4];
        if (!count || count>64 || !rt.memory().contains(address,count*16u)) { rt.stop("Invalid controller sample buffer"); return; }
        std::uint32_t buttons=0;for(const auto &[when,mask]:input_script_) {if(when>virtual_time_us_)break;buttons=mask;}
        const auto live=controller_provider_?controller_provider_():ControllerSample{};
        buttons |= live.buttons;
        for (std::uint32_t i=0;i<count;++i) { rt.memory().zero(address+i*16u,16); rt.memory().store32(address+i*16u+4,buttons); rt.memory().store32(address+i*16u,static_cast<std::uint32_t>(++virtual_time_us_)); rt.memory().store8(address+i*16u+8,controller_mode_?live.x:128); rt.memory().store8(address+i*16u+9,controller_mode_?live.y:128); }
        ctx.set_gpr(2,count);
    });
    for (auto nid : {0x79D1C3FAu,0xB435DEC5u,0xBFA98062u,0x3EE30821u,0x34B9FA9Eu,0x920F104Au,0xC2DF770Eu}) runtime.register_hle("UtilsForUser",nid,[](auto &,auto &ctx) { ctx.set_gpr(2,0); });
    runtime.register_hle("ThreadManForUser",0xEA748E31u,[this](auto &rt,auto &ctx) {
        if (!current_ || ((ctx.gpr[4]|ctx.gpr[5]) & ~0x80004000u)) { rt.stop("Unsupported thread attribute change"); return; }
        auto &thread=threads_.at(current_); thread.attributes=(thread.attributes & ~ctx.gpr[4])|ctx.gpr[5]; ctx.set_gpr(2,0);
    });
    runtime.nids().add("SysMemUserForUser", 0x358CA1BBu, "sceKernelSetCompiledSdkVersion660");
    runtime.register_hle("SysMemUserForUser", 0x358CA1BBu,
        [this](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            // This firmware-specific entry accepts the 6.60 SDK family. A
            // mismatch resets/hangs on hardware; preserve an explicit boundary.
            if ((ctx.gpr[4] >> 16u) != 0x0606u) {
                rt.stop("Unsupported SDK family for sceKernelSetCompiledSdkVersion660: " +
                        psprecomp::hex32(ctx.gpr[4]));
                return;
            }
            sdk_version_ = ctx.gpr[4];
            ctx.set_gpr(2, 0);
        });
    runtime.register_hle("SysMemUserForUser", 0xF77D77CBu,
        [this](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            compiler_version_ = ctx.gpr[4];
            ctx.set_gpr(2, 0);
        });
    runtime.register_hle("ThreadManForUser", 0x446D8DE6u,
        [this](auto &rt, auto &ctx) { create_thread(rt, ctx); });
    runtime.register_hle("ThreadManForUser", 0xF475845Du,
        [this](auto &rt, auto &ctx) { start_thread(rt, ctx); });
    runtime.register_hle("ThreadManForUser", 0xAA73C935u,
        [this](auto &rt, auto &ctx) { exit_thread(rt, ctx, static_cast<std::int32_t>(ctx.gpr[4]), false); });
    runtime.register_hle("ThreadManForUser", 0x809CE29Bu,
        [this](auto &rt, auto &ctx) { exit_thread(rt, ctx, static_cast<std::int32_t>(ctx.gpr[4]), true); });
    runtime.register_hle("ThreadManForUser", 0x293B45B8u,
        [this](auto &rt, auto &ctx) {
            if (!current_) { rt.stop("Bootstrap threads not configured"); return; }
            ctx.set_gpr(2, static_cast<std::uint32_t>(current_));
        });
    runtime.register_hle("SysMemUserForUser", 0x237DBD4Fu,
        [this](auto &rt, auto &ctx) {
            const auto type = ctx.gpr[6], size = ctx.gpr[7];
            if (!current_ || ctx.gpr[4] != 2 || (type != 0 && type != 1 && type != 3 && type != 4)) {
                rt.stop("Bootstrap AllocPartitionMemory: unsupported partition or allocation type"); return;
            }
            const auto alignment = type >= 3 ? ctx.gpr[8] : 256u;
            if (!size || size > UINT32_MAX - 255u || !alignment || (alignment & (alignment - 1u))) {
                rt.stop("Bootstrap AllocPartitionMemory: invalid size/alignment"); return;
            }
            std::string name;
            for (std::uint32_t i = 0; i < 32; ++i) {
                const auto address = static_cast<std::uint64_t>(ctx.gpr[5]) + i;
                if (address > UINT32_MAX || !rt.memory().contains(static_cast<std::uint32_t>(address))) {
                    rt.stop("Bootstrap AllocPartitionMemory: invalid name pointer"); return;
                }
                const auto ch = rt.memory().load8(static_cast<std::uint32_t>(address));
                if (!ch) break;
                if (i == 31) { rt.stop("Bootstrap AllocPartitionMemory: name exceeds diagnostic limit"); return; }
                name.push_back(static_cast<char>(ch));
            }
            const auto address = memory_.allocate(size, alignment, type == 1 || type == 4);
            if (!address) { rt.stop("Bootstrap AllocPartitionMemory: reserved arena exhausted"); return; }
            const auto uid = next_uid_++;
            blocks_.emplace(uid, Block{name, *address, (size + 255u) & ~255u});
            ctx.set_gpr(2, static_cast<std::uint32_t>(uid));
        });
    runtime.register_hle("SysMemUserForUser", 0x9D9A5BA1u,
        [this](auto &rt, auto &ctx) {
            const auto it = blocks_.find(static_cast<std::int32_t>(ctx.gpr[4]));
            if (it == blocks_.end()) { rt.stop("Bootstrap GetBlockHeadAddr: unknown block UID"); return; }
            ctx.set_gpr(2, it->second.address);
        });
    runtime.register_hle("SysMemUserForUser", 0xB6D61D02u,
        [this](auto &rt, auto &ctx) {
            const auto it = blocks_.find(static_cast<std::int32_t>(ctx.gpr[4]));
            if (it == blocks_.end()) { rt.stop("Bootstrap FreePartitionMemory: unknown block UID"); return; }
            memory_.release(it->second.address); blocks_.erase(it); ctx.set_gpr(2, 0);
        });
    runtime.register_hle("SysMemUserForUser", 0xA291F107u,
        [this](auto &, auto &ctx) { ctx.set_gpr(2, memory_.max_free()); });
    runtime.register_hle("SysMemUserForUser", 0xF919F628u,
        [this](auto &, auto &ctx) { ctx.set_gpr(2, memory_.total_free()); });
    runtime.nids().add("ModuleMgrForUser", 0xD8B73127u, "sceKernelGetModuleIdByAddress");
    runtime.register_hle("ModuleMgrForUser", 0xD8B73127u,
        [this](auto &rt, auto &ctx) {
            if (module_) {
                for (const auto &[address, size] : module_->segments) {
                    if (ctx.gpr[4] >= address && ctx.gpr[4] - address < size) {
                        ctx.set_gpr(2, static_cast<std::uint32_t>(module_->uid)); return;
                    }
                }
            }
            rt.stop("Bootstrap GetModuleIdByAddress: address outside registered module");
        });
    runtime.nids().add("ModuleMgrForUser", 0x748CBED9u, "sceKernelQueryModuleInfo");
    runtime.register_hle("ModuleMgrForUser", 0x748CBED9u,
        [this](auto &rt, auto &ctx) {
            const auto output = ctx.gpr[5];
            if (!module_ || ctx.gpr[4] != static_cast<std::uint32_t>(module_->uid)) {
                rt.stop("Bootstrap QueryModuleInfo: unknown module UID"); return;
            }
            if ((output & 3u) || !rt.memory().contains(output, 96) || rt.memory().load32(output) != 96) {
                rt.stop("Bootstrap QueryModuleInfo: expected writable 96-byte structure"); return;
            }
            std::array<std::uint8_t, 96> data{};
            const auto word = [&data](std::size_t offset, std::uint32_t value) {
                for (unsigned i = 0; i < 4; ++i) data[offset+i] = static_cast<std::uint8_t>(value >> (8*i));
            };
            word(0, 96); data[4] = static_cast<std::uint8_t>(module_->segments.size());
            for (std::size_t i = 0; i < module_->segments.size(); ++i) {
                word(8+4*i, module_->segments[i].first); word(24+4*i, module_->segments[i].second);
            }
            word(40, module_->entry); word(44, module_->info.gp); word(48, module_->text_address);
            word(52, module_->text_size); word(56, module_->data_size); word(60, module_->bss_size);
            data[64] = static_cast<std::uint8_t>(module_->info.attributes);
            data[65] = static_cast<std::uint8_t>(module_->info.attributes >> 8);
            data[66] = module_->info.minor_version; data[67] = module_->info.major_version;
            std::copy_n(module_->info.name.begin(), std::min<std::size_t>(module_->info.name.size(), 27), data.begin()+68);
            rt.memory().copy_in(output, data); ctx.set_gpr(2, 0);
        });
    runtime.register_hle("Kernel_Library",0xA089ECA4u,[](auto &rt,auto &ctx){
        const auto out=ctx.gpr[4],count=ctx.gpr[6];if(!rt.memory().contains(out,count)){rt.stop("Invalid kernel memset buffer");return;}
        std::vector<std::uint8_t> bytes(count,static_cast<std::uint8_t>(ctx.gpr[5]));rt.memory().copy_in(out,bytes);ctx.set_gpr(2,out);
    });
    runtime.register_hle("Kernel_Library",0x1839852Au,[](auto &rt,auto &ctx){
        const auto out=ctx.gpr[4],in=ctx.gpr[5],count=ctx.gpr[6];if(!rt.memory().contains(out,count)||!rt.memory().contains(in,count)){rt.stop("Invalid kernel memcpy buffer");return;}
        std::vector<std::uint8_t> bytes(count);rt.memory().copy_out(in,bytes);rt.memory().copy_in(out,bytes);ctx.set_gpr(2,out);
    });
    runtime.register_hle("sceUtility",0x2A2B3DE0u,[this](auto &rt,auto &ctx){
        const auto id=ctx.gpr[4];if(id<0x300 || id>0x303){rt.stop("Unsupported utility module "+psprecomp::hex32(id));return;}
        ctx.set_gpr(2,utility_modules_.insert(id).second?0:0x80111102);
    });
    runtime.register_hle("sceUtility",0xE49BFE92u,[this](auto &,auto &ctx){ctx.set_gpr(2,utility_modules_.erase(ctx.gpr[4])?0:0x80111103);});
    runtime.register_hle("ModuleMgrForUser",0x977DE386u,[](auto &rt,auto &ctx){rt.stop("Module load requested: "+rt.memory().read_c_string(ctx.gpr[4],1024));});
    runtime.nids().add("scePower",0x469989ADu,"scePowerSetClockFrequency630");
    for(auto nid:{0x469989ADu,0x737486F2u,0xEBD177D6u})runtime.register_hle("scePower",nid,[this](auto &rt,auto &ctx){
        if(!ctx.gpr[4]||ctx.gpr[4]>333||!ctx.gpr[5]||ctx.gpr[5]>333||!ctx.gpr[6]||ctx.gpr[6]>166){rt.stop("Invalid PSP clock frequencies");return;}
        pll_mhz_=ctx.gpr[4];cpu_mhz_=ctx.gpr[5];bus_mhz_=ctx.gpr[6];ctx.set_gpr(2,0);
    });
    runtime.register_hle("scePower",0x2085D15Du,[](auto &,auto &ctx){ctx.set_gpr(2,100);});
    runtime.register_hle("scePower",0x87440F5Eu,[](auto &,auto &ctx){ctx.set_gpr(2,1);});
    runtime.register_hle("scePower",0xD3075926u,[](auto &,auto &ctx){ctx.set_gpr(2,0);});
    runtime.register_hle("sceSuspendForUser",0x090CCB3Fu,[this](auto &,auto &ctx){last_power_tick_=virtual_time_us_;ctx.set_gpr(2,0);});
    runtime.register_hle("UtilsForUser",0x27CC57F0u,[](auto &rt,auto &ctx){const auto now=static_cast<std::uint32_t>(std::time(nullptr));if(ctx.gpr[4])rt.memory().store32(ctx.gpr[4],now);ctx.set_gpr(2,now);});
    runtime.register_hle("UtilsForUser",0x91E4F6A7u,[this](auto &,auto &ctx){ctx.set_gpr(2,static_cast<std::uint32_t>(virtual_time_us_));});
    runtime.register_hle("UtilsForUser",0x71EC4271u,[](auto &rt,auto &ctx){
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(ctx.gpr[4]){rt.memory().store32(ctx.gpr[4],us/1000000);rt.memory().store32(ctx.gpr[4]+4,us%1000000);}
        if(ctx.gpr[5])rt.memory().zero(ctx.gpr[5],8);ctx.set_gpr(2,0);
    });
    runtime.register_hle("sceRtc",0xE7C27D1Bu,[](auto &rt,auto &ctx){
        const auto p=ctx.gpr[4];if(!rt.memory().contains(p,16)){rt.stop("Invalid RTC output");return;}
        const auto now=std::chrono::system_clock::now();const auto time=std::chrono::system_clock::to_time_t(now);std::tm date{};localtime_r(&time,&date);
        for (const auto [offset,value]:{std::pair{0,1900+date.tm_year},{2,1+date.tm_mon},{4,date.tm_mday},{6,date.tm_hour},{8,date.tm_min},{10,date.tm_sec}})rt.memory().store16(p+offset,value);
        rt.memory().store32(p+12,std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000);ctx.set_gpr(2,0);
    });
    runtime.nids().add("Kernel_Library", 0x092968F4u, "sceKernelCpuSuspendIntr");
    runtime.register_hle("Kernel_Library", 0x092968F4u,
        [this](auto &, auto &ctx) { ctx.set_gpr(2, interrupts_enabled_ ? 1u : 0u); interrupts_enabled_ = false; });
    const auto resume_interrupts = [this](auto &rt, auto &ctx) {
        if (ctx.gpr[4] > 1u) { rt.stop("Bootstrap ResumeIntr: unknown interrupt state token"); return; }
        interrupts_enabled_ = ctx.gpr[4] != 0;
        if (current_ && interrupts_enabled_) schedule(rt, ctx, false);
        // Void API: preserve v0 unless a context switch installs another CPU.
    };
    runtime.nids().add("Kernel_Library", 0x5F10D406u, "sceKernelCpuResumeIntr");
    runtime.nids().add("Kernel_Library", 0x3B84732Du, "sceKernelCpuResumeIntrWithSync");
    runtime.register_hle("Kernel_Library", 0x5F10D406u, resume_interrupts);
    runtime.register_hle("Kernel_Library", 0x3B84732Du, resume_interrupts);
    runtime.register_hle("Kernel_Library", 0xB55249D2u,
        [this](auto &, auto &ctx) { ctx.set_gpr(2, interrupts_enabled_ ? 1u : 0u); });
    runtime.register_hle("SysMemUserForUser", 0x13A5ABEFu,
        [](auto &rt, auto &ctx) {
            try {
                const auto message = format_guest_printf(rt.memory(), ctx);
                std::cout << "[guest] " << message;
                if (message.empty() || message.back() != '\n') std::cout << '\n';
                ctx.set_gpr(2, static_cast<std::uint32_t>(message.size()));
            } catch (const std::exception &e) { rt.stop(std::string("Bootstrap KernelPrintf: ")+e.what()); }
        });
    runtime.register_hle("ff3Bootstrap", 0,
        [this](auto &rt, auto &ctx) { exit_thread(rt, ctx, static_cast<std::int32_t>(ctx.gpr[2]), false); });
}
std::int32_t BootstrapHle::register_module(psprecomp::Runtime &rt, const psprecomp::Elf32Image &elf,
                                         const psprecomp::PspModuleInfo &info, std::uint32_t load_base) {
    if (module_) throw psprecomp::Error("Bootstrap module already registered");
    Module candidate;
    candidate.info = info; candidate.entry = elf.runtime_entry(load_base);
    for (std::size_t i = 0; i < elf.segments().size(); ++i) {
        const auto &segment = elf.segments()[i];
        if (segment.type != 1 || !segment.memory_size) continue;
        const auto address = elf.segment_runtime_address(i, load_base);
        if (segment.file_size > segment.memory_size || !rt.memory().contains(address, segment.memory_size) ||
            candidate.segments.size() == 4) throw psprecomp::Error("Invalid bootstrap module segment");
        for (const auto &[other, size] : candidate.segments) {
            if (static_cast<std::uint64_t>(address) < static_cast<std::uint64_t>(other) + size &&
                static_cast<std::uint64_t>(other) < static_cast<std::uint64_t>(address) + segment.memory_size)
                throw psprecomp::Error("Overlapping bootstrap module segments");
        }
        candidate.segments.emplace_back(address, segment.memory_size);
        if (segment.flags & 1u) {
            if (!candidate.text_address) candidate.text_address = address;
            candidate.text_size += segment.file_size;
        } else candidate.data_size += segment.file_size;
        candidate.bss_size += segment.memory_size - segment.file_size;
    }
    if (candidate.segments.empty()) throw psprecomp::Error("Bootstrap module has no loaded segments");
    candidate.uid = next_uid_++;
    module_ = std::move(candidate);
    return module_->uid;
}
void BootstrapHle::configure_threads(psprecomp::Runtime &rt, std::uint32_t floor,
                                    std::uint32_t ceiling, std::uint32_t priority) {
    if (!threads_.empty() || floor >= ceiling || (floor & 255u) || (ceiling & 255u) ||
        !rt.memory().contains(floor, ceiling - floor) || rt.has_function(return_trampoline) ||
        (return_trampoline >= floor && return_trampoline < ceiling) || priority < 8 || priority > 119)
        throw psprecomp::Error("Invalid bootstrap thread configuration");
    const auto loader_tls = rt.cpu().gpr[29];
    if ((loader_tls & 15u) || !rt.memory().contains(loader_tls, 256) ||
        (static_cast<std::uint64_t>(loader_tls) < ceiling && static_cast<std::uint64_t>(loader_tls)+256 > floor))
        throw psprecomp::Error("Invalid bootstrap loader TLS reservation");
    memory_.initialize(floor, ceiling);
    rt.memory().zero(loader_tls, 256);
    rt.cpu().gpr[26] = loader_tls;
    Thread loader;
    loader.name = "module_start"; loader.priority = priority; loader.state = State::running;
    current_ = 1;
    rt.cpu().gpr[31] = return_trampoline;
    loader.context = rt.cpu();
    threads_.emplace(current_, std::move(loader));
    rt.register_function(return_trampoline, [](auto &runtime, auto &ctx) {
        runtime.invoke_import("ff3Bootstrap", 0, ctx);
    }, "ff3_thread_return");
    psprecomp::set_runtime_thread_identity(current_, "module_start");
}
void BootstrapHle::create_thread(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
    // PSP EABI passes arguments five and six in t0/t1.
    const auto entry = ctx.gpr[5], priority = ctx.gpr[6], size = ctx.gpr[7];
    const auto attributes = ctx.gpr[8], options = ctx.gpr[9];
    const auto reject = [&rt](const char *reason) { rt.stop(std::string("Bootstrap CreateThread: ") + reason); };
    if (!current_) { reject("threads not configured"); return; }
    if (threads_.size() >= 256) { reject("diagnostic thread limit"); return; }
    if (options || (attributes & ~0x80004000u)) { reject("unsupported options/attributes"); return; }
    if (priority < 8 || priority > 119 || !rt.has_function(entry) || entry == return_trampoline) {
        reject("unsupported priority or unregistered entry"); return;
    }
    if (size < 0x200u || size > memory_.max_free()) { reject("stack outside reserved arena"); return; }
    const auto rounded = (size + 255u) & ~255u;
    if (rounded > memory_.max_free()) { reject("aligned stack outside reserved arena"); return; }
    std::string name;
    for (std::uint32_t i = 0; i < 32; ++i) {
        const auto address = static_cast<std::uint64_t>(ctx.gpr[4]) + i;
        if (address > UINT32_MAX || !rt.memory().contains(static_cast<std::uint32_t>(address))) {
            reject("invalid name pointer"); return;
        }
        const auto ch = rt.memory().load8(static_cast<std::uint32_t>(address));
        if (!ch) break;
        if (i == 31) { reject("name exceeds diagnostic limit"); return; }
        name.push_back(static_cast<char>(ch));
    }
    const auto stack = memory_.allocate(rounded, 256u, true);
    if (!stack) { reject("stack allocation failed"); return; }
    Thread thread;
    thread.name = name; thread.entry = entry; thread.priority = priority; thread.attributes = attributes;
    thread.stack_top = *stack + rounded; thread.stack_bottom = *stack;
    thread.gp = ctx.gpr[28];
    // Diagnostic stack fill; reserve 256 bytes above the argument/frame area.
    std::vector<std::uint8_t> fill(rounded, 0xFFu);
    rt.memory().copy_in(thread.stack_bottom, fill);
    const auto uid = next_uid_++;
    threads_.emplace(uid, std::move(thread));
    ctx.set_gpr(2, static_cast<std::uint32_t>(uid));
}
void BootstrapHle::start_thread(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
    if (!current_) { rt.stop("Bootstrap StartThread: no running thread"); return; }
    const auto it = threads_.find(static_cast<std::int32_t>(ctx.gpr[4]));
    const auto count = ctx.gpr[5], source = ctx.gpr[6];
    if (it == threads_.end() || it->second.state != State::dormant || !it->second.stack_top) {
        rt.stop("Bootstrap StartThread: unknown or non-dormant thread"); return;
    }
    auto &thread = it->second;
    if (count > thread.stack_top - thread.stack_bottom - 0x100u ||
        (count && !rt.memory().contains(source, count))) {
        rt.stop("Bootstrap StartThread: arguments exceed stack or guest memory"); return;
    }
    const auto arg_address = (thread.stack_top - 0x100u - count) & ~15u;
    if (arg_address < thread.stack_bottom + 0x40u) {
        rt.stop("Bootstrap StartThread: no space for entry frame"); return;
    }
    std::vector<std::uint8_t> args(count);
    if (count) { rt.memory().copy_out(source, args); rt.memory().copy_in(arg_address, args); }
    thread.context = {};
    thread.context.pc = thread.entry;
    thread.context.gpr[4] = count; thread.context.gpr[5] = count ? arg_address : 0;
    // The CRT stores its per-thread pointer at k0+4. Keep this reserved
    // record separate from copied arguments and the downward-growing stack.
    rt.memory().zero(thread.stack_top - 0x100u, 0x100u);
    thread.context.gpr[26] = thread.stack_top - 0x100u;
    thread.context.gpr[28] = thread.gp; thread.context.gpr[29] = arg_address - 0x40u;
    thread.context.gpr[31] = return_trampoline;
    thread.state = State::ready; thread.ready_order = ++ready_order_;
    ctx.set_gpr(2, 0);
    schedule(rt, ctx, false);
}
void BootstrapHle::schedule(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx, bool exiting, bool at_import) {
    if (!interrupts_enabled_ || interrupt_frame_) return;
    auto selected = threads_.end();
    for (auto it = threads_.begin(); it != threads_.end(); ++it) {
        if (it->second.state != State::ready) continue;
        if (selected == threads_.end() || it->second.priority < selected->second.priority ||
            (it->second.priority == selected->second.priority && it->second.ready_order < selected->second.ready_order))
            selected = it;
    }
    if (selected == threads_.end()) {
        if (exiting) {
            std::uint64_t wake=UINT64_MAX; bool waiting=false;
            for (const auto &[uid,thread] : threads_) if (thread.state==State::waiting) {
                waiting=true; if (thread.wait==Wait::delay) wake=std::min(wake,thread.wake_time);
            }
            if (hardware_hook_ && waiting) {
                const auto step = wake==UINT64_MAX ? 16667ull : std::min<std::uint64_t>(16667,wake-virtual_time_us_);
                virtual_time_us_+=step;
                ctx=threads_.at(current_).context;
                if (hardware_hook_(rt,ctx,true)) return;
            }
            if (wake!=UINT64_MAX) {
                virtual_time_us_=std::max(virtual_time_us_,wake);
                for (auto &[uid,thread] : threads_) if (thread.state==State::waiting && thread.wait==Wait::delay && thread.wake_time<=virtual_time_us_) { thread.state=State::ready; thread.wait=Wait::none; thread.ready_order=++ready_order_; }
                schedule(rt,ctx,true); return;
            }
            if (waiting) { rt.stop("Bootstrap scheduler deadlock: no ready thread or timer"); return; }
            current_=0; psprecomp::set_runtime_thread_identity(-1, ""); rt.stop("Bootstrap threads completed");
        }
        return;
    }
    if (!exiting) {
        auto &previous = threads_.at(current_);
        if (selected->second.priority >= previous.priority) return;
        previous.context = ctx;
        previous.context.pc = at_import ? ctx.gpr[31] : ctx.pc; // Resume after the import, not inside its wrapper.
        previous.state = State::ready; previous.ready_order = ++ready_order_;
    }
    current_ = selected->first;
    selected->second.state = State::running;
    ctx = selected->second.context;
    // Invalidate the generated import wrapper's caller token before returning.
    psprecomp::set_runtime_thread_identity(current_, selected->second.name);
}
void BootstrapHle::exit_thread(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx,
                              std::int32_t status, bool remove) {
    if (!interrupts_enabled_) { rt.stop("Bootstrap ExitThread: interrupts disabled"); return; }
    const auto it = threads_.find(current_);
    if (it == threads_.end()) { rt.stop("Bootstrap ExitThread: no running thread"); return; }
    it->second.context = ctx; it->second.exit_status = status; it->second.state = State::dormant;
    for(auto &[uid,t]:threads_)if(t.state==State::waiting&&t.wait==Wait::thread_end&&t.wait_id==static_cast<std::uint32_t>(current_)){
        t.state=State::ready;t.wait=Wait::none;t.context.gpr[2]=0;t.ready_order=++ready_order_;
    }
    if (remove) {
        if (it->second.stack_top) memory_.release(it->second.stack_bottom);
        threads_.erase(it);
    }
    schedule(rt, ctx, true);
}
}
