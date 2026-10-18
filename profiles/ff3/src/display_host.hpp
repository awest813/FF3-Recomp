#pragma once
#include "psprecomp/runtime.hpp"
#include "bootstrap_hle.hpp"
#include "ge_renderer.hpp"
#include "framebuffer_capture.hpp"
#include <map>
#include <deque>
#include <filesystem>
namespace ff3 {
class DisplayHost {
public:
    DisplayHost();
    void install(psprecomp::Runtime &, BootstrapHle &);
    using Presenter = std::function<void(psprecomp::Runtime &,std::span<const std::uint8_t>)>;
    void set_presenter(Presenter presenter) { presenter_=std::move(presenter); }
    void set_capture_enabled(bool enabled) { capture_enabled_=enabled; }
private:
    struct SubInterrupt { std::uint32_t entry,common; bool enabled{}; };
    std::map<std::uint32_t,SubInterrupt> interrupts_;
    std::uint64_t next_vblank_{16667};
    struct List { std::uint32_t pc, stall, callback; bool done{}; std::vector<std::uint32_t> calls; std::uint32_t signal{}; };
    struct Callback { std::uint32_t signal, signal_arg, finish, finish_arg; };
    struct PendingCallback { std::uint32_t entry, id, common; };
    void run_lists(psprecomp::Runtime &);
    void callbacks(psprecomp::AllegrexContext &);
    void frame(psprecomp::Runtime &);
    std::array<std::uint32_t,256> commands_{};
    vcs::GeTransformState transform_{};
    std::uint32_t vertex_{}, index_{}, offset_{}, translation_{}, next_list_{1},next_callback_{1};
    std::uint64_t revision_{1}, frames_{};
    bool bbox_visible_{true};
    std::map<std::uint32_t,List> lists_;
    std::map<std::uint32_t,Callback> callbacks_;
    std::deque<PendingCallback> pending_;
    std::map<std::int32_t,psprecomp::AllegrexContext> callers_;
    vcs::FramebufferDescription framebuffer_{0,480,272,512,3};
    std::vector<std::uint8_t> last_frame_;
    std::filesystem::path frame_dir_;
    Presenter presenter_;
    bool capture_enabled_{true};
};
}
