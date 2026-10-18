#include "psprecomp/runtime.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/sha256.hpp"
#include "psprecomp/common.hpp"
#include "ff3_identity.hpp"
#include "bootstrap_hle.hpp"
#include "display_host.hpp"
#include "file_host.hpp"
#include "audio_host.hpp"
#include "utility_host.hpp"
#include "movie_host.hpp"
#include "savedata_host.hpp"
#include "host_system.hpp"
#ifdef FF3_NATIVE_WINDOW
#include "native_window.hpp"
#endif
#ifdef FF3_NATIVE_FONT
#include "font_module.hpp"
#endif
#include <algorithm>
#include <charconv>
#include <iostream>
#include <string_view>
#include <unordered_set>
#include <limits>
#include <cstdlib>
namespace { std::unordered_set<std::uint32_t> import_addresses; std::uint64_t dispatch_count{}; ff3::BootstrapHle *active_hle{}; bool trace_dispatch{true}; }
namespace ff3 { void install_sas(psprecomp::Runtime &); void install_savedata(psprecomp::Runtime &,const std::filesystem::path &); void install_atrac(psprecomp::Runtime &,const std::filesystem::path &); }
namespace psprecomp { void register_generated_functions(Runtime &); }
using namespace psprecomp;
int main(int argc, char **argv) {
    try {
#ifdef FF3_PLAYER_ELF
        char app_name[]="ff3_player", default_elf[]=FF3_PLAYER_ELF, play_mode[]="--play";
        char *player_args[]{app_name,default_elf,play_mode,nullptr};
        if (argc==1) { argc=3; argv=player_args; }
        // GUI launches do not inherit the diagnostic driver's chaining setting.
        ff3::set_environment("PSPRECOMP_NO_CHAIN","1");
#endif
        if (argc < 2 || argc > 4) throw Error("Usage: ff3_probe <EBOOT.ELF> [max_dispatches=64|--play] [bootstrap|none]");
        const bool interactive=argc>=3 && std::string_view(argv[2])=="--play";
        std::uint64_t limit = 64;
        if (interactive) limit=std::numeric_limits<std::uint64_t>::max();
        else if (argc >= 3) {
            const std::string_view text(argv[2]);
            const auto result = std::from_chars(text.data(), text.data()+text.size(), limit);
            if (result.ec != std::errc{} || result.ptr != text.data()+text.size() || limit == 0 || limit > 100000000)
                throw Error("max_dispatches must be in 1..100000000");
        }
        if (sha256_file(argv[1]) != ff3::input_sha256) throw Error("ELF SHA-256 differs from the compiled corpus");
        const std::string_view hle_mode = argc == 4 ? argv[3] : "bootstrap";
        if (hle_mode != "bootstrap" && hle_mode != "none") throw Error("HLE mode must be bootstrap or none");
        if (interactive && hle_mode!="bootstrap") throw Error("Interactive play requires bootstrap HLE");
#ifndef FF3_NATIVE_WINDOW
        if (interactive) throw Error("Build with FF3_BUILD_WINDOW=ON for interactive play");
#endif
        Runtime rt;
#include "import_names.inc"
        ff3::BootstrapHle hle;
        ff3::DisplayHost display;
        ff3::AudioHost audio;
        ff3::UtilityHost utility;
#ifdef FF3_NATIVE_WINDOW
        std::unique_ptr<ff3::NativeWindow> window;
        if (interactive) {
            ff3::set_environment("PSPRECOMP_NO_CHAIN","1");
            const auto autoplay=std::getenv("FF3_AUTOPLAY_UNTIL_MS");
            const auto autoplay_until=autoplay?std::stoull(autoplay)*1000:0;
            window=std::make_unique<ff3::NativeWindow>(autoplay_until);
            display.set_capture_enabled(std::getenv("FF3_FRAME_DIR")!=nullptr);
            display.set_presenter([&,autoplay_until,autoplay_active=autoplay_until!=0](auto &runtime,auto rgb) mutable {
                if (autoplay_active && hle.virtual_time()>=autoplay_until) {
                    ff3::unset_environment("FF3_DIALOG_RESPONSE");
                    autoplay_active=false;
                }
                if (!window->present(rgb,hle.virtual_time())) runtime.stop("Native window closed");
            });
            hle.set_controller_provider([&]{return window->controller();});
            audio.set_output([&](unsigned channel,auto pcm){window->audio(channel,pcm);},
                             [&](unsigned channel){window->clear_audio(channel);});
            utility.set_dialog_handler([&](const auto &text,auto options){return window->dialog(text,options);});
            utility.set_keyboard_handler([&](const auto &description,const auto &initial,auto limit){return window->keyboard(description,initial,limit);});
            trace_dispatch=std::getenv("FF3_TRACE_DISPATCH")!=nullptr;
        }
#endif
        if (const auto trace=std::getenv("FF3_TRACE_DISPATCH"); trace && std::string_view(trace)=="0") trace_dispatch=false;
        ff3::MovieHost movie(std::filesystem::path(argv[1]).parent_path()/"disc");
        ff3::FileHost files(std::filesystem::path(argv[1]).parent_path()/"disc");
        if (hle_mode == "bootstrap") {files.install(rt);ff3::install_sas(rt);ff3::install_atrac(rt,std::filesystem::path(argv[1]).parent_path()/"disc");}
        if (hle_mode == "bootstrap") display.install(rt,hle);
        if (hle_mode == "bootstrap") {hle.install(rt);audio.install(rt,hle);utility.install(rt);movie.install(rt,hle);ff3::install_savedata(rt,std::filesystem::path(argv[1]).parent_path()/"ms0/PSP/SAVEDATA");}
#ifdef FF3_NATIVE_WINDOW
        if (window) ff3::set_save_slot_handler([&](const auto &slots,bool saving){return window->save_slot(slots,saving);});
#endif
        // Symbol identity only; this does not install an HLE implementation.
        // uofw exports identify this firmware-specific NID as the 6.60 setter.
        rt.nids().add("SysMemUserForUser", 0x358CA1BBu, "sceKernelSetCompiledSdkVersion660");
        rt.nids().add("ModuleMgrForUser", 0xD8B73127u, "sceKernelGetModuleIdByAddress");
        const auto elf = Elf32Image::from_file(argv[1]);
        const auto relocations = elf.load_and_relocate(rt.memory(), ff3::load_base);
        if (relocations.invalid || relocations.unsupported) throw Error("Relocations failed");
        const auto module = elf.find_module_info(rt.memory(), ff3::load_base);
        if (!module) throw Error("Module info unavailable");
        const auto module_uid = hle_mode == "bootstrap" ? hle.register_module(rt, elf, *module, ff3::load_base) : -1;
        for (const auto &entry : elf.scan_imports(rt.memory(), *module)) import_addresses.insert(entry.stub_address);
        register_generated_functions(rt);
        auto &c = rt.cpu();
        // Diagnostic launch assumptions, pending a reference startup capture.
        // Keep the stack at the top of user RAM and reject loaded-image overlap.
        constexpr std::uint32_t stack_bottom = 0x09FF0000u, stack_top = 0x09FFFF00u;
        std::uint64_t image_end = ff3::load_base;
        for (std::size_t i=0; i<elf.segments().size(); ++i) {
            const auto &s = elf.segments()[i];
            if (s.type != 1u) continue;
            const std::uint64_t start = elf.segment_runtime_address(i, ff3::load_base);
            image_end = std::max(image_end, start + s.memory_size);
            if (start <= ff3::BootstrapHle::return_trampoline && start + s.memory_size > ff3::BootstrapHle::return_trampoline)
                throw Error("Diagnostic return trampoline overlaps loaded segment");
            if (start < stack_top && start + s.memory_size > stack_bottom)
                throw Error("Diagnostic stack overlaps loaded segment");
        }
        c.gpr[29] = stack_top; c.gpr[28] = module->gp; c.gpr[31] = 0;
        c.gpr[4] = 0; c.gpr[5] = 0;
        if (hle_mode == "bootstrap") {
            if (image_end > stack_bottom - 256u) throw Error("No bootstrap stack arena");
            hle.configure_threads(rt, static_cast<std::uint32_t>((image_end + 255u) & ~255ull), stack_bottom);
        }
#ifdef FF3_NATIVE_FONT
        ff3::FontModule font(std::filesystem::path(argv[1]).parent_path()/"libfont.fixed.elf");
        if (hle_mode=="bootstrap") font.install(rt,hle);
#endif
        set_runtime_pre_dispatch_hook([](Runtime &, AllegrexContext &ctx, std::uint32_t pc, std::int32_t uid) {
            ++dispatch_count;
            if (!trace_dispatch) return;
            if (dispatch_count > 2000 && dispatch_count % 10000 && !import_addresses.contains(pc)) return;
            std::cout << "dispatch " << hex32(pc) << " ra=" << hex32(ctx.gpr[31])
                      << " a0=" << hex32(ctx.gpr[4]) << " a1=" << hex32(ctx.gpr[5])
                      << " uid=" << uid << " k0=" << hex32(ctx.gpr[26]) << " a2=" << hex32(ctx.gpr[6]) << " a3=" << hex32(ctx.gpr[7]) << " a4=" << hex32(ctx.gpr[8]) << " a5=" << hex32(ctx.gpr[9]) << '\n';
        });
        std::cout << std::unitbuf << (interactive?"FF3 interactive host; HLE mode=":"FF3 diagnostic probe; HLE mode=") << hle_mode << "\n"
                  << "sha256=" << ff3::input_sha256 << " entry=" << hex32(elf.runtime_entry(ff3::load_base))
                  << " base=" << hex32(ff3::load_base) << " gp=" << hex32(c.gpr[28])
                  << " sp=" << hex32(c.gpr[29]) << " k0=" << hex32(c.gpr[26])
                  << " module_uid=" << module_uid << " args=(0,0)\n"
                  << "relocations=" << relocations.total << " registered_entries=" << rt.function_count() << '\n';
        if (hle_mode == "bootstrap") {
            active_hle=&hle;
            set_runtime_post_dispatch_hook([](Runtime &runtime,AllegrexContext &ctx,std::uint32_t,std::int32_t) { active_hle->advance(runtime,ctx,1); });
        }
        rt.run(elf.runtime_entry(ff3::load_base), limit);
        std::cout << "dispatch_count=" << dispatch_count << '\n';
        std::cout << "stop=" << rt.stop_reason() << " pc=" << hex32(c.pc)
                  << " ra=" << hex32(c.gpr[31]) << " v0=" << hex32(c.gpr[2]) << '\n';
        if (hle.sdk_version()) std::cout << "sdk_version=" << hex32(*hle.sdk_version()) << '\n';
        if (hle.compiler_version()) std::cout << "compiler_version=" << hex32(*hle.compiler_version()) << '\n';
        for (const auto &[uid, thread] : hle.threads()) {
            std::cout << "thread uid=" << uid << " name=" << thread.name << " entry=" << hex32(thread.entry)
                      << " priority=" << thread.priority << " attr=" << hex32(thread.attributes)
                      << " stack=" << hex32(thread.stack_bottom) << "-" << hex32(thread.stack_top)
                      << " state=" << (thread.state == ff3::BootstrapHle::State::running ? "running" :
                                        thread.state == ff3::BootstrapHle::State::ready ? "ready" :
                                        thread.state == ff3::BootstrapHle::State::waiting ? "waiting" : "dormant") << '\n';
        }
        if (interactive && rt.stop_reason()=="Native window closed") return 0;
        return 2; // A diagnostic stop is not a successful native game boot.
    } catch (const std::exception &e) { std::cerr << "ff3_probe: " << e.what() << '\n'; return 1; }
}
