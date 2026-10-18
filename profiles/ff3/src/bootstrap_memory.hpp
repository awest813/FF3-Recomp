#pragma once
#include <cstdint>
#include <map>
#include <optional>
namespace ff3 {
// One owned interval shared by thread stacks and user partition blocks.
class BootstrapMemory {
public:
    void initialize(std::uint32_t begin, std::uint32_t end);
    std::optional<std::uint32_t> allocate(std::uint32_t size, std::uint32_t alignment, bool high);
    bool reserve(std::uint32_t address, std::uint32_t size);
    bool release(std::uint32_t address);
    std::uint32_t total_free() const;
    std::uint32_t max_free() const;
private:
    std::map<std::uint32_t, std::uint32_t> free_, used_;
};
}
