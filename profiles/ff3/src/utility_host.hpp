#pragma once
#include "psprecomp/runtime.hpp"
#include <optional>
namespace ff3 {
class UtilityHost {
public:
    void install(psprecomp::Runtime &);
    using DialogHandler = std::function<std::uint32_t(const std::string &,std::uint32_t)>;
    void set_dialog_handler(DialogHandler handler) { dialog_handler_=std::move(handler); }
    using KeyboardHandler = std::function<std::optional<std::u16string>(const std::u16string &,const std::u16string &,std::uint32_t)>;
    void set_keyboard_handler(KeyboardHandler handler) { keyboard_handler_=std::move(handler); }
private:
    void install_keyboard(psprecomp::Runtime &);
    std::uint32_t message_{},status_{},updates_{};
    DialogHandler dialog_handler_;
    KeyboardHandler keyboard_handler_;
    std::uint32_t keyboard_params_{},keyboard_data_{},keyboard_output_{},keyboard_limit_{},keyboard_status_{};
    std::u16string keyboard_description_,keyboard_initial_;
};
}
