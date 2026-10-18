#include "bootstrap_memory.hpp"
#include <algorithm>
#include <stdexcept>
namespace ff3 {
void BootstrapMemory::initialize(std::uint32_t begin, std::uint32_t end) {
    if (!free_.empty() || !used_.empty() || begin >= end || (begin & 255u) || (end & 255u))
        throw std::invalid_argument("Invalid bootstrap memory arena");
    free_.emplace(begin, end - begin);
}
std::optional<std::uint32_t> BootstrapMemory::allocate(std::uint32_t size, std::uint32_t alignment, bool high) {
    if (!size || size > UINT32_MAX - 255u || !alignment || (alignment & (alignment - 1u))) return {};
    size = (size + 255u) & ~255u;
    alignment = std::max(alignment, 256u);
    auto chosen = free_.end();
    std::uint32_t address = 0;
    for (auto it = free_.begin(); it != free_.end(); ++it) {
        if (it->second < size) continue;
        const std::uint64_t end = static_cast<std::uint64_t>(it->first) + it->second;
        const std::uint64_t mask = static_cast<std::uint64_t>(alignment) - 1u;
        const auto candidate = high ? (end - size) & ~mask : (static_cast<std::uint64_t>(it->first) + mask) & ~mask;
        if (candidate < it->first || candidate + size > end) continue;
        chosen = it; address = static_cast<std::uint32_t>(candidate);
        if (!high) break;
    }
    if (chosen == free_.end()) return {};
    const auto begin = chosen->first, end = begin + chosen->second;
    free_.erase(chosen);
    if (begin < address) free_.emplace(begin, address - begin);
    if (address + size < end) free_.emplace(address + size, end - address - size);
    used_.emplace(address, size);
    return address;
}
bool BootstrapMemory::reserve(std::uint32_t address,std::uint32_t size) {
    if (!size || (address&255u) || (size&255u)) return false;
    const auto upper=free_.upper_bound(address);if(upper==free_.begin())return false;
    const auto it=std::prev(upper);const auto begin=it->first;const std::uint64_t end=static_cast<std::uint64_t>(begin)+it->second;
    if (static_cast<std::uint64_t>(address)+size>end) return false;
    free_.erase(it);if(begin<address)free_.emplace(begin,address-begin);
    if(address+size<end)free_.emplace(address+size,static_cast<std::uint32_t>(end-address-size));
    used_.emplace(address,size);return true;
}
bool BootstrapMemory::release(std::uint32_t address) {
    const auto owned = used_.find(address);
    if (owned == used_.end()) return false;
    auto size = owned->second;
    used_.erase(owned);
    auto next = free_.lower_bound(address);
    if (next != free_.end() && address + size == next->first) {
        size += next->second; next = free_.erase(next);
    }
    if (next != free_.begin()) {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == address) {
            address = previous->first; size += previous->second; free_.erase(previous);
        }
    }
    free_.emplace(address, size);
    return true;
}
std::uint32_t BootstrapMemory::total_free() const {
    std::uint32_t total = 0;
    for (const auto &[address, size] : free_) total += size;
    return total;
}
std::uint32_t BootstrapMemory::max_free() const {
    std::uint32_t maximum = 0;
    for (const auto &[address, size] : free_) maximum = std::max(maximum, size);
    return maximum;
}
}
