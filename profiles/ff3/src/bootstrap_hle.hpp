#pragma once
#include "psprecomp/runtime.hpp"
#include "bootstrap_memory.hpp"
#include "psprecomp/elf32.hpp"
#include <map>
#include <optional>
#include <vector>
#include <set>
namespace ff3 {
// Per-process bootstrap state. Keep this object alive as long as its Runtime.
// Cooperative PSP threads with virtual timers and interrupt delivery.
class BootstrapHle {
public:
    enum class State { dormant, ready, running, waiting };
    enum class Wait { none, delay, sleep, semaphore, event, mutex, thread_end };
    struct Thread {
        std::string name;
        std::uint32_t entry{}, priority{}, attributes{}, stack_bottom{}, stack_top{}, gp{};
        State state{State::dormant};
        std::uint64_t ready_order{};
        std::int32_t exit_status{};
        Wait wait{Wait::none};
        std::uint64_t wake_time{};
        std::uint32_t wait_id{},wait_count{},wake_count{},wait_mode{},wait_output{};
        psprecomp::AllegrexContext context;
    };
    static constexpr std::uint32_t return_trampoline = 0x08000000u;
    void install(psprecomp::Runtime &runtime);
    void delay(psprecomp::Runtime &,psprecomp::AllegrexContext &,std::uint64_t us,std::uint32_t result=0);
    void advance(psprecomp::Runtime &,psprecomp::AllegrexContext &,std::uint64_t microseconds,bool at_import=false);
    bool reserve_static(std::uint32_t address,std::uint32_t size) { return memory_.reserve(address,size); }
    std::int32_t allocate_uid() { return next_uid_++; }
    using HardwareHook = std::function<bool(psprecomp::Runtime &, psprecomp::AllegrexContext &, bool)>;
    void set_hardware_hook(HardwareHook hook) { hardware_hook_=std::move(hook); }
    struct ControllerSample { std::uint32_t buttons{}; std::uint8_t x{128}, y{128}; };
    using ControllerProvider = std::function<ControllerSample()>;
    void set_controller_provider(ControllerProvider provider) { controller_provider_=std::move(provider); }
    [[nodiscard]] std::uint64_t virtual_time() const { return virtual_time_us_; }
    bool interrupt(psprecomp::AllegrexContext &, std::uint32_t entry, std::uint32_t sub, std::uint32_t common);
    std::int32_t register_module(psprecomp::Runtime &, const psprecomp::Elf32Image &,
                                 const psprecomp::PspModuleInfo &, std::uint32_t load_base);
    // Call after loading the image and initializing the loader context. Reserve
    // 256 bytes at/above its SP for the loader TLS record. The
    // caller reserves [stack_floor, stack_ceiling) for thread stacks and user partition blocks.
    void configure_threads(psprecomp::Runtime &runtime, std::uint32_t stack_floor,
                           std::uint32_t stack_ceiling, std::uint32_t loader_priority = 32);
    [[nodiscard]] std::optional<std::uint32_t> sdk_version() const { return sdk_version_; }
    [[nodiscard]] std::optional<std::uint32_t> compiler_version() const { return compiler_version_; }
    [[nodiscard]] const std::map<std::int32_t, Thread> &threads() const { return threads_; }
    [[nodiscard]] std::int32_t current_thread() const { return current_; }
private:
    void install_synchronization(psprecomp::Runtime &);
    void block(psprecomp::Runtime &,psprecomp::AllegrexContext &,std::uint32_t result=0);
    bool dispatch_callback(psprecomp::AllegrexContext &, bool resume_import);
    void install_callbacks(psprecomp::Runtime &);
    void install_pools(psprecomp::Runtime &);
    void create_thread(psprecomp::Runtime &, psprecomp::AllegrexContext &);
    void start_thread(psprecomp::Runtime &, psprecomp::AllegrexContext &);
    void exit_thread(psprecomp::Runtime &, psprecomp::AllegrexContext &, std::int32_t status, bool remove);
    void schedule(psprecomp::Runtime &, psprecomp::AllegrexContext &, bool exiting, bool at_import=true);
    struct Module {
        std::int32_t uid{};
        psprecomp::PspModuleInfo info;
        std::uint32_t entry{}, text_address{}, text_size{}, data_size{}, bss_size{};
        std::vector<std::pair<std::uint32_t, std::uint32_t>> segments;
    };
    std::optional<Module> module_;
    std::set<std::uint32_t> utility_modules_;
    std::optional<std::uint32_t> sdk_version_, compiler_version_;
    std::map<std::int32_t, Thread> threads_;
    BootstrapMemory memory_;
    struct Block { std::string name; std::uint32_t address, size; };
    std::map<std::int32_t, Block> blocks_;
    struct FixedPool {
        std::string name;
        std::uint32_t address{}, stride{};
        std::vector<bool> allocated;
    };
    std::map<std::int32_t, FixedPool> pools_;
    struct Callback { std::string name; std::uint32_t entry, common, pending{}, argument{}; std::int32_t owner; };
    struct CallbackFrame { std::int32_t uid; psprecomp::AllegrexContext caller; };
    std::map<std::int32_t, Callback> callbacks_;
    std::map<std::int32_t, CallbackFrame> callback_frames_;
    std::optional<std::int32_t> exit_callback_;
    std::map<std::uint32_t,std::int32_t> power_callbacks_;
    std::int32_t current_{}, next_uid_{2};
    struct EventFlag { std::uint32_t bits,attributes; };
    std::map<std::int32_t,EventFlag> events_;
    struct Semaphore { std::uint32_t count, maximum, attributes; };
    std::map<std::int32_t,Semaphore> semaphores_;
    std::map<std::uint32_t,std::int32_t> mutexes_;
    std::uint64_t ready_order_{};
    bool interrupts_enabled_{true};
    bool volatile_memory_locked{};
    HardwareHook hardware_hook_;
    ControllerProvider controller_provider_;
    std::optional<psprecomp::AllegrexContext> interrupt_frame_;
    std::uint32_t pll_mhz_{222},cpu_mhz_{222},bus_mhz_{111};
    std::uint64_t last_power_tick_{};
    std::uint32_t controller_mode_{}, controller_cycle_{};
    std::vector<std::pair<std::uint64_t,std::uint32_t>> input_script_;
    std::uint64_t virtual_time_us_{};
    std::uint32_t idle_cancel_x_{},idle_cancel_y_{},language_{1},confirm_button_{1};
};
}
