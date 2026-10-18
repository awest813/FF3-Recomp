#include "bootstrap_hle.hpp"
#include "guest_printf.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace psprecomp;
using ff3::BootstrapHle;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint32_t entry = 0x08804000, name = 0x08805000, arena_begin = 0x09000000, arena_end = 0x09100000;
struct Fixture {
    Runtime rt;
    BootstrapHle hle;
    Fixture() {
        hle.install(rt);
        rt.register_function(entry, [](auto &, auto &ctx) { ctx.gpr[2] = 42; ctx.pc = ctx.gpr[31]; }, "fixture_thread");
        rt.cpu().gpr[28] = 0x12345678; rt.cpu().gpr[29] = 0x09FFFF00;
        hle.configure_threads(rt, arena_begin, arena_end);
        const std::array<std::uint8_t, 5> text{'t','e','s','t',0}; rt.memory().copy_in(name, text);
    }
    ~Fixture() { set_runtime_thread_identity(-1, ""); }
    void call(const char *library, std::uint32_t nid, std::initializer_list<std::uint32_t> args) {
        unsigned i=4; for (auto arg : args) rt.cpu().gpr[i++] = arg;
        rt.invoke_import(library, nid, rt.cpu());
    }
    std::uint32_t create(std::uint32_t priority=32, std::uint32_t size=4096, std::uint32_t attr=0x80004000) {
        call("ThreadManForUser", 0x446D8DE6, {name, entry, priority, size, attr, 0}); return rt.cpu().gpr[2];
    }
    void start(std::uint32_t uid, std::uint32_t count=0, std::uint32_t source=0) {
        call("ThreadManForUser", 0xF475845D, {uid, count, source});
    }
    std::uint32_t allocate(std::uint32_t type, std::uint32_t size, std::uint32_t alignment=256) {
        call("SysMemUserForUser", 0x237DBD4F, {2, name, type, size, alignment}); return rt.cpu().gpr[2];
    }
    std::uint32_t head(std::uint32_t uid) {
        call("SysMemUserForUser", 0x9D9A5BA1, {uid}); return rt.cpu().gpr[2];
    }
};
Elf32Image module_fixture(std::uint32_t second_address = 0x1000) {
    std::vector<std::uint8_t> data(0x220);
    const auto put16 = [&data](unsigned offset, std::uint16_t value) {
        data.at(offset) = static_cast<std::uint8_t>(value); data.at(offset+1) = static_cast<std::uint8_t>(value >> 8);
    };
    const auto put32 = [&data](unsigned offset, std::uint32_t value) {
        for (unsigned i=0; i<4; ++i) data.at(offset+i) = static_cast<std::uint8_t>(value >> (8*i));
    };
    data[0]=0x7F; data[1]='E'; data[2]='L'; data[3]='F'; data[4]=1; data[5]=1; data[6]=1;
    put16(16, 0xFFA0); put16(18, 8); put32(20, 1); put32(24, 4); put32(28, 52);
    put16(40, 52); put16(42, 32); put16(44, 2); put16(46, 40);
    for (unsigned i=0; i<2; ++i) {
        const auto at = 52+32*i;
        put32(at, 1); put32(at+4, 0x100+0x100*i); put32(at+8, i ? second_address : 0);
        put32(at+16, 16); put32(at+20, i ? 64 : 32); put32(at+24, i ? 6 : 5); put32(at+28, 4);
    }
    return Elf32Image::from_bytes(std::move(data));
}
void module_tests() {
    for (auto offset : {0u, 31u, 0x1000u, 0x103Fu}) {
        Fixture f; auto elf = module_fixture();
        const auto uid = f.hle.register_module(f.rt, elf, {}, entry);
        f.call("ModuleMgrForUser", 0xD8B73127, {entry + offset});
        check(!f.rt.stopped() && f.rt.cpu().gpr[2] == static_cast<std::uint32_t>(uid), "module lookup missed segment boundary/BSS");
        check(f.create() != static_cast<std::uint32_t>(uid), "module/thread UID collision");
    }
    for (auto offset : {32u, 0x800u, 0x1040u, 0xFFFFFFFFu}) {
        Fixture f; f.hle.register_module(f.rt, module_fixture(), {}, entry);
        f.rt.cpu().gpr[2] = 123;
        f.call("ModuleMgrForUser", 0xD8B73127, {entry + offset});
        check(f.rt.stopped() && f.rt.cpu().gpr[2] == 123, "module lookup accepted a gap/outside address");
    }
    {
        Fixture f; PspModuleInfo info;
        info.name="synthetic"; info.attributes=0x1234; info.minor_version=2; info.major_version=3; info.gp=entry+8;
        const auto uid = f.hle.register_module(f.rt, module_fixture(), info, entry);
        const auto out = name+0x100;
        f.rt.memory().store32(out-4, 0x1234); f.rt.memory().store32(out+96, 0x5678); f.rt.memory().store32(out, 96);
        f.call("ModuleMgrForUser", 0x748CBED9, {static_cast<std::uint32_t>(uid), out});
        auto &memory = f.rt.memory();
        check(!f.rt.stopped() && f.rt.cpu().gpr[2] == 0, "module query failed");
        check(memory.load32(out)==96 && memory.load32(out+4)==2, "module size/segment count/reserved");
        check(memory.load32(out+8)==entry && memory.load32(out+12)==entry+0x1000 && memory.load32(out+16)==0, "module segment addresses");
        check(memory.load32(out+24)==32 && memory.load32(out+28)==64 && memory.load32(out+32)==0, "module segment sizes");
        check(memory.load32(out+40)==entry+4 && memory.load32(out+44)==entry+8, "module entry/GP");
        check(memory.load32(out+48)==entry && memory.load32(out+52)==16 && memory.load32(out+56)==16 && memory.load32(out+60)==64, "module text/data/BSS");
        check(memory.load32(out+64)==0x03021234 && memory.read_c_string(out+68)=="synthetic", "module attributes/version/name");
        check(memory.load32(out-4)==0x1234 && memory.load32(out+96)==0x5678, "module query overwrote canary");
        memory.store32(out, 64); memory.store32(out+4, 0xABCD);
        f.call("ModuleMgrForUser", 0x748CBED9, {static_cast<std::uint32_t>(uid), out});
        check(f.rt.stopped() && memory.load32(out+4)==0xABCD, "unsupported query size partially wrote buffer");
    }
    {
        Fixture f; const auto uid=f.hle.register_module(f.rt, module_fixture(), {}, entry);
        f.call("ModuleMgrForUser", 0x748CBED9, {static_cast<std::uint32_t>(uid), 0x09FFFFE0});
        check(f.rt.stopped(), "module query accepted truncated output");
    }
    {
        Fixture f; f.hle.register_module(f.rt, module_fixture(), {}, entry);
        f.call("ModuleMgrForUser", 0x748CBED9, {99, name});
        check(f.rt.stopped(), "module query accepted invalid UID");
    }
    {
        Fixture f; bool rejected = false;
        try { f.hle.register_module(f.rt, module_fixture(16), {}, entry); } catch (const std::exception &) { rejected=true; }
        check(rejected, "overlapping module segments accepted");
        check(f.hle.register_module(f.rt, module_fixture(), {}, entry) == 2, "failed registration consumed UID");
    }
}
void interrupt_tests() {
    Fixture f;
    f.call("Kernel_Library", 0x092968F4, {}); const auto outer=f.rt.cpu().gpr[2];
    f.call("Kernel_Library", 0x092968F4, {}); const auto inner=f.rt.cpu().gpr[2];
    check(outer==1 && inner==0, "nested interrupt state tokens");
    const auto uid=f.create(16); f.start(uid);
    check(f.hle.current_thread()==1, "interrupt-masked start switched context");
    f.call("Kernel_Library", 0x5F10D406, {inner});
    f.call("Kernel_Library", 0xB55249D2, {});
    check(f.rt.cpu().gpr[2]==0 && f.hle.current_thread()==1, "inner resume prematurely enabled interrupts");
    f.rt.cpu().gpr[31]=entry+4; f.rt.cpu().gpr[2]=0xFEED;
    const auto token=capture_runtime_execution_context();
    f.call("Kernel_Library", 0x3B84732D, {outer});
    check(!runtime_execution_context_matches(token) && f.hle.current_thread()==static_cast<std::int32_t>(uid), "resume did not schedule pending ready thread");
    f.call("ThreadManForUser", 0xAA73C935, {0});
    check(f.rt.cpu().pc==entry+4 && f.rt.cpu().gpr[2]==0xFEED, "void resume lost caller return state");
    f.call("Kernel_Library", 0xB55249D2, {});
    check(f.rt.cpu().gpr[2]==1, "interrupt state not restored");
    f.call("Kernel_Library", 0x5F10D406, {2});
    check(f.rt.stopped(), "invalid interrupt token accepted");
}
void printf_tests() {
    Fixture f;
    const auto put_string=[&](std::uint32_t address, std::string_view text) {
        for (std::size_t i=0; i<text.size(); ++i) f.rt.memory().store8(address+static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(text[i]));
        f.rt.memory().store8(address+static_cast<std::uint32_t>(text.size()), 0);
    };
    const auto format=[&](std::string_view text, std::initializer_list<std::uint32_t> args) {
        put_string(name, text); f.rt.cpu().gpr[4]=name;
        unsigned reg=5; for (auto arg:args) f.rt.cpu().gpr[reg++]=arg;
        return ff3::format_guest_printf(f.rt.memory(), f.rt.cpu());
    };
    check(format("programAreaSize = %d\n", {0x1EBCC0})=="programAreaSize = 2014400\n", "observed guest printf");
    check(format("%d %u %#08x %X %%", {0x80000000,0xFFFFFFFF,0x42,0xABCD})=="-2147483648 4294967295 0x000042 ABCD %", "printf integer formatting");
    put_string(name+0x200, "sample");
    check(format("%-6.3s:%+05d:%#o", {name+0x200,12,8})=="sam   :+0012:010", "printf padding/precision/sign");
    check(format("%*.*s %ld", {static_cast<std::uint32_t>(-5),2,name+0x200,0xFFFFFFFF})=="sa    -1", "printf dynamic width/precision/long");
    check(format("%.0s", {0xFFFFFFFF}).empty(), "zero-precision string dereferenced");
    const auto rejected=[&](std::string_view text, std::initializer_list<std::uint32_t> args) {
        try { format(text,args); } catch (const std::exception &) { return true; } return false;
    };
    check(rejected("%n", {name+0x200}) && rejected("%f", {0}) && rejected("%ls", {name+0x200}) && rejected("%lld", {0}), "unsupported printf conversion accepted");
    check(rejected("%s", {0xFFFFFFFF}) && rejected("%1025d", {0}) && rejected("%", {}), "printf pointer/field/format bounds");
    check(rejected("%d%d%d%d%d%d%d%d", {1,2,3,4,5,6,7}), "printf read unimplemented stack argument");
    check(rejected("%1024d%1024d%1024d%1024dX", {0,0,0,0}), "printf output bound");
    check(f.rt.memory().read_c_string(name+0x200)=="sample", "printf wrote guest memory");
}
void pool_tests() {
    {
        Fixture f; const auto thread=f.create(); const auto output=name+0x100;
        f.call("SysMemUserForUser", 0xF919F628, {}); const auto available=f.rt.cpu().gpr[2];
        f.call("ThreadManForUser", 0xC07BB470, {name,2,0,17,2,0}); const auto pool=f.rt.cpu().gpr[2];
        check(!f.rt.stopped() && pool!=thread, "fixed pool creation/UID ownership");
        f.call("ThreadManForUser", 0xD979E9BF, {pool,output,0}); const auto first=f.rt.memory().load32(output);
        f.call("ThreadManForUser", 0x623AE665, {pool,output}); const auto second=f.rt.memory().load32(output);
        check(first==arena_begin && second==first+32 && second+17 <= f.hle.threads().at(thread).stack_bottom, "pool block stride/non-overlap");
        f.call("ThreadManForUser", 0xF6414A71, {pool,first});
        f.call("ThreadManForUser", 0xD979E9BF, {pool,output,0});
        check(f.rt.memory().load32(output)==first && f.rt.cpu().gpr[2]==0, "pool block reuse");
        f.call("ThreadManForUser", 0xED1410E0, {pool});
        f.call("SysMemUserForUser", 0xF919F628, {});
        check(f.rt.cpu().gpr[2]==available, "deleted pool not reclaimed");
        f.call("ThreadManForUser", 0x623AE665, {pool,output});
        check(f.rt.stopped(), "deleted pool UID accepted");
    }
    {
        Fixture f; const auto output=name+0x100;
        f.call("ThreadManForUser", 0xC07BB470, {name,2,0,256,1,0}); const auto pool=f.rt.cpu().gpr[2];
        f.call("ThreadManForUser", 0x623AE665, {pool,output});
        f.rt.memory().store32(output,0xBAD);
        f.call("ThreadManForUser", 0x623AE665, {pool,output});
        check(f.rt.stopped() && f.rt.memory().load32(output)==0xBAD, "exhausted pool overwrote result");
    }
    for (const auto pointer : {arena_begin+1,arena_begin+256}) {
        Fixture f; const auto output=name+0x100;
        f.call("ThreadManForUser", 0xC07BB470, {name,2,0,256,1,0}); const auto pool=f.rt.cpu().gpr[2];
        f.call("ThreadManForUser", 0x623AE665, {pool,output});
        f.call("ThreadManForUser", 0xF6414A71, {pool,pointer});
        check(f.rt.stopped(), "pool accepted interior/outside block pointer");
    }
    {
        Fixture f;
        f.call("ThreadManForUser", 0xC07BB470, {name,2,0,0x80000000,4,0});
        check(f.rt.stopped(), "pool multiplication overflow accepted");
        f.call("SysMemUserForUser", 0xF919F628, {});
        check(f.rt.cpu().gpr[2]==arena_end-arena_begin, "failed pool creation consumed memory");
    }
    {
        Fixture f;
        f.call("ThreadManForUser", 0xC07BB470, {name,2,0,256,1,0}); const auto pool=f.rt.cpu().gpr[2];
        f.call("ThreadManForUser", 0xD979E9BF, {pool,0x09FFFFFE,0});
        check(f.rt.stopped(), "pool accepted invalid output pointer");
    }
}
void allocator_tests() {
    ff3::BootstrapMemory arena; arena.initialize(0x1000, 0x6000);
    check(arena.allocate(1, 1, false) == 0x1000, "low allocation/rounding");
    check(arena.allocate(0x200, 0x1000, true) == 0x5000, "high aligned allocation");
    check(arena.allocate(0x100, 0x2000, false) == 0x2000, "low aligned allocation");
    const auto available = arena.total_free();
    check(!arena.allocate(UINT32_MAX, 256, false) && !arena.allocate(256, 3, false), "overflow/non-power-of-two rejected");
    check(!arena.allocate(0, 256, false) && !arena.allocate(0x6000, 256, false), "zero/exhaustion rejected");
    check(arena.total_free() == available, "failed allocation changed arena");
    check(!arena.release(0x1100), "unowned free accepted");
    check(arena.release(0x2000) && arena.release(0x1000) && arena.release(0x5000), "free failed");
    check(!arena.release(0x5000) && arena.total_free() == 0x5000 && arena.max_free() == 0x5000, "coalescing/double free");
    check(arena.allocate(0x5000, 256, true) == 0x1000 && arena.total_free() == 0, "whole arena reuse");
}
void synchronization_tests() {
    { // Joining blocks the caller and resumes only when the target exits.
        Fixture f;const auto tid=f.create(33);f.start(tid);f.rt.cpu().gpr[31]=entry+0x444;
        f.call("ThreadManForUser",0x840E8133,{static_cast<std::uint32_t>(tid),0});
        check(f.hle.current_thread()==tid,"thread join did not dispatch target");
        f.call("ThreadManForUser",0xAA73C935,{42});
        check(f.hle.current_thread()==1&&f.rt.cpu().pc==entry+0x444&&f.rt.cpu().gpr[2]==0,"thread join did not resume caller");
        f.call("ThreadManForUser",0x9FA03CD3,{static_cast<std::uint32_t>(tid)});
        check(!f.hle.threads().contains(tid),"joined thread was not deleted");
    }
    { // A high-priority waiter must resume with the import's return PC and result.
        Fixture f;
        f.call("ThreadManForUser",0xD6DA4BA1,{name,0,0,1,0});const auto sem=f.rt.cpu().gpr[2];
        const auto tid=f.create(16);f.start(tid);f.rt.cpu().gpr[31]=entry+0x40;
        f.call("ThreadManForUser",0x4E3A1105,{sem,1,0});
        check(f.hle.current_thread()==1,"semaphore did not block");
        f.call("ThreadManForUser",0x3F53E640,{sem,1});
        check(f.hle.current_thread()==tid && f.rt.cpu().pc==entry+0x40 && f.rt.cpu().gpr[2]==0,"semaphore did not resume waiter");
    }
    {
        Fixture f;const auto area=name+64;
        f.call("ThreadManForUser",0x19CFF145,{area,name,0,1,0});
        const auto tid=f.create(16);f.start(tid);f.rt.cpu().gpr[31]=entry+0x80;
        f.call("Kernel_Library",0xBEA46419,{area,1,0});
        check(f.hle.current_thread()==1 && f.rt.memory().load32(area+12)==1,"mutex wait accounting");
        f.call("Kernel_Library",0x15B6446B,{area,1});
        check(f.hle.current_thread()==tid && f.rt.memory().load32(area+4)==tid && f.rt.cpu().pc==entry+0x80,"mutex ownership transfer");
    }
    {
        Fixture f;f.rt.cpu().pc=entry+0x100;f.rt.cpu().gpr[31]=entry+0x104;
        f.call("ThreadManForUser",0xE81CAF8F,{name,entry,0x1234});const auto cb=f.rt.cpu().gpr[2];
        f.call("ThreadManForUser",0xC11BA8C4,{cb,0x5678});
        f.call("ThreadManForUser",0x68DA9E36,{100});
        check(f.rt.cpu().pc==entry && f.rt.cpu().gpr[4]==1 && f.rt.cpu().gpr[5]==0x5678 && f.rt.cpu().gpr[6]==0x1234,"CB wait did not execute callback");
        f.rt.cpu().gpr[2]=0;f.call("ff3Bootstrap",1,{});
        check(f.rt.cpu().pc==entry+0x100 && f.rt.cpu().gpr[4]==100,"callback failed to restore wait arguments");
        f.call("ThreadManForUser",0x68DA9E36,{100});
        check(!f.rt.stopped() && f.hle.virtual_time()==100 && f.rt.cpu().pc==entry+0x104,"delay did not finish after callback");
    }
    { // Interrupt-side signaling defers a context switch until the handler returns.
        Fixture f;f.call("ThreadManForUser",0x55C20A00,{name,0x200,0,0});const auto event=f.rt.cpu().gpr[2];
        const auto tid=f.create(16);f.start(tid);f.rt.cpu().gpr[31]=entry+0x200;
        f.call("ThreadManForUser",0x402FCF22,{event,1,0x20,0,0});
        f.rt.cpu().pc=entry+0x300;f.rt.cpu().gpr[16]=0xABCD;
        check(f.hle.interrupt(f.rt.cpu(),entry,0,42),"interrupt was not entered");
        f.call("ThreadManForUser",0x1FB15A32,{event,1});
        check(f.hle.current_thread()==1,"interrupt switched thread before return");
        f.call("ff3Bootstrap",2,{});
        check(f.hle.current_thread()==tid && f.rt.cpu().pc==entry+0x200,"interrupt did not dispatch awakened thread");
        f.call("ThreadManForUser",0xAA73C935,{0});
        check(f.rt.cpu().pc==entry+0x300 && f.rt.cpu().gpr[16]==0xABCD,"interrupted CPU state lost");
    }
}
void live_controller_tests() {
    Fixture f;
    ff3::BootstrapHle::ControllerSample live{0x4020,255,0};
    f.hle.set_controller_provider([&]{return live;});
    for (auto nid : {0x1F803938u,0x3A622550u}) {
        f.call("sceCtrl",nid,{name,2});
        check(f.rt.cpu().gpr[2]==2,"controller sample count");
        for (auto offset : {0u,16u}) {
            check(f.rt.memory().load32(name+offset+4)==0x4020,"live controller buttons lost");
            check(f.rt.memory().load8(name+offset+8)==128 && f.rt.memory().load8(name+offset+9)==128,
                  "digital sampling exposed analog axes");
        }
    }
    f.call("sceCtrl",0x1F4011E6,{1});
    f.call("sceCtrl",0x1F803938,{name,1});
    check(f.rt.memory().load8(name+8)==255 && f.rt.memory().load8(name+9)==0,"live analog axes lost");
    live={};
    f.call("sceCtrl",0x1F803938,{name,1});
    check(f.rt.memory().load32(name+4)==0 && f.rt.memory().load8(name+8)==128 &&
          f.rt.memory().load8(name+9)==128,"released live controls remained held");
    f.hle.set_controller_provider({});
    f.call("sceCtrl",0x1F803938,{name,1});
    check(f.rt.memory().load32(name+4)==0,"headless controller fallback");
}
int main() {
 try {
    allocator_tests(); module_tests(); interrupt_tests(); printf_tests(); pool_tests();
    {
        Fixture f;
        f.call("SysMemUserForUser", 0x358CA1BB, {0x06060010});
        check(f.hle.sdk_version() == 0x06060010 && f.rt.cpu().gpr[2] == 0, "SDK version");
        f.call("SysMemUserForUser", 0xF77D77CB, {0x30306});
        check(f.hle.compiler_version() == 0x30306 && f.rt.cpu().gpr[2] == 0, "compiler version");
        f.rt.cpu().gpr[2] = 123;
        f.call("SysMemUserForUser", 0x358CA1BB, {0x05050010});
        check(f.rt.stopped() && f.hle.sdk_version() == 0x06060010 && f.rt.cpu().gpr[2] == 123, "invalid SDK changed state");
    }
    {
        Fixture f; const auto uid = f.create();
        check(uid == 2 && f.hle.threads().at(uid).state == BootstrapHle::State::dormant, "CreateThread must be dormant");
        f.rt.memory().store32(name + 0x100, 0x1234ABCD);
        f.start(uid, 4, name + 0x100);
        check(f.hle.current_thread() == 1, "equal priority start preempted loader");
        const auto &thread = f.hle.threads().at(uid);
        const auto arg = thread.context.gpr[5];
        const auto tls = thread.context.gpr[26];
        check(tls == thread.stack_top-256 && f.rt.memory().load32(tls+4)==0, "thread TLS reservation");
        check(tls != f.hle.threads().at(1).context.gpr[26] && arg+4 <= tls, "thread TLS overlaps loader/arguments");
        f.rt.memory().store32(tls+4, 0xCAFE);
        check(f.rt.memory().load32(f.hle.threads().at(1).context.gpr[26]+4)==0, "thread TLS not isolated");
        f.rt.memory().store32(name + 0x100, 0);
        check(f.rt.memory().load32(arg) == 0x1234ABCD && thread.context.gpr[4] == 4, "thread arguments not owned copy");
        check(thread.context.gpr[28] == 0x12345678 && (thread.context.gpr[29] & 15) == 0, "thread GP/SP");
        check(arg >= thread.stack_bottom && arg + 4 <= thread.stack_top && thread.context.gpr[29] < arg, "argument/frame bounds");
        const auto token = capture_runtime_execution_context();
        f.rt.cpu().gpr[2] = 7;
        f.rt.run(BootstrapHle::return_trampoline, 8);
        check(!runtime_execution_context_matches(token), "thread switch failed to invalidate wrapper token");
        check(f.rt.stop_reason() == "Bootstrap threads completed", "thread return scheduling");
        check(f.hle.threads().at(1).exit_status == 7 && f.hle.threads().at(uid).exit_status == 42, "thread exit status");
    }
    {
        Fixture f; const auto uid = f.create(16);
        f.rt.cpu().gpr[31] = entry + 4; f.rt.cpu().gpr[16] = 0xBEEF;
        f.start(uid);
        check(f.hle.current_thread() == static_cast<std::int32_t>(uid) && f.rt.cpu().pc == entry, "higher priority start did not preempt");
        f.call("ThreadManForUser", 0x293B45B8, {});
        check(f.rt.cpu().gpr[2] == uid, "current thread ID");
        const auto stack = f.hle.threads().at(uid).stack_bottom;
        f.call("ThreadManForUser", 0x809CE29B, {11});
        check(!f.hle.threads().contains(uid) && f.hle.current_thread() == 1, "ExitDeleteThread did not resume loader");
        check(f.rt.cpu().pc == entry + 4 && f.rt.cpu().gpr[16] == 0xBEEF && f.rt.cpu().gpr[2] == 0, "caller context/return lost");
        const auto replacement = f.create();
        check(f.hle.threads().at(replacement).stack_bottom == stack, "deleted stack not reclaimed");
    }
    {
        Fixture f; const auto first = f.create(), second = f.create();
        f.start(second); f.start(first);
        f.call("ThreadManForUser", 0xAA73C935, {0});
        check(f.hle.current_thread() == static_cast<std::int32_t>(second), "ready FIFO ignored");
        f.call("ThreadManForUser", 0xAA73C935, {0});
        check(f.hle.current_thread() == static_cast<std::int32_t>(first), "next ready thread not selected");
    }
    {
        Fixture f; const auto uid = f.create(); f.start(uid); f.start(uid);
        check(f.rt.stopped() && f.hle.current_thread() == 1, "duplicate start accepted");
    }
    {
        Fixture f; const auto uid = f.create(); f.start(uid, UINT32_MAX, name);
        check(f.rt.stopped() && f.hle.threads().at(uid).state == BootstrapHle::State::dormant, "oversized arguments mutated thread");
    }
    {
        Fixture f; f.create(32, UINT32_MAX);
        check(f.rt.stopped() && f.hle.threads().size() == 1, "stack overflow mutated threads");
    }
    {
        Fixture f; f.create(32, 4096, 0x100000);
        check(f.rt.stopped() && f.hle.threads().size() == 1, "unsupported attribute accepted");
    }
    {
        Fixture f;
        const auto tid = f.create(), bid = f.allocate(3, 4096, 4096), address = f.head(bid);
        check(bid != tid && address % 4096 == 0 && address >= arena_begin, "partition UID/alignment");
        check(address + 4096 <= f.hle.threads().at(tid).stack_bottom, "partition overlaps thread");
        f.call("SysMemUserForUser", 0xF919F628, {});
        check(f.rt.cpu().gpr[2] == arena_end - arena_begin - 8192, "shared free memory accounting");
        f.call("SysMemUserForUser", 0xB6D61D02, {bid});
        check(f.rt.cpu().gpr[2] == 0 && f.head(f.allocate(0, 4096)) == address, "partition block not reclaimed");
        f.call("SysMemUserForUser", 0x9D9A5BA1, {tid});
        check(f.rt.stopped(), "thread UID accepted as partition UID");
    }
    synchronization_tests();
    live_controller_tests();
    std::cout << "FF3 bootstrap HLE execution passed\n";
 } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
