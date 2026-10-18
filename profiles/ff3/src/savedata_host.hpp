#pragma once
#include "psprecomp/runtime.hpp"
#include <functional>
#include <optional>
#include <vector>
namespace ff3 {
struct SaveSlot { std::string name; bool occupied{}; };
using SaveSlotHandler = std::function<std::optional<std::size_t>(const std::vector<SaveSlot> &, bool)>;
void install_savedata(psprecomp::Runtime &, const std::filesystem::path &);
void set_save_slot_handler(SaveSlotHandler);
}
