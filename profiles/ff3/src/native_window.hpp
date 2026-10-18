#pragma once
#include "bootstrap_hle.hpp"
#include "savedata_host.hpp"
#include <memory>
#include <span>

namespace ff3 {
// AppKit calls and guest execution both run on the process's main thread.
class NativeWindow {
public:
    explicit NativeWindow(std::uint64_t autoplay_until_us=0);
    ~NativeWindow();
    NativeWindow(const NativeWindow &)=delete;
    NativeWindow &operator=(const NativeWindow &)=delete;
    bool present(std::span<const std::uint8_t> rgb,std::uint64_t guest_us);
    BootstrapHle::ControllerSample controller() const;
    void audio(unsigned channel,std::span<const std::int16_t> pcm);
    void clear_audio(unsigned channel);
    std::uint32_t dialog(const std::string &text,std::uint32_t options);
    std::optional<std::size_t> save_slot(const std::vector<SaveSlot> &slots, bool saving);
    std::optional<std::u16string> keyboard(const std::u16string &description,const std::u16string &initial,std::uint32_t limit);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
