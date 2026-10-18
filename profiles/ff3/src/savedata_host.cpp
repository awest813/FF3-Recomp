// SPDX-License-Identifier: MIT
// Savedata adapter derived from this repository's VCS host.
#include "psprecomp/runtime.hpp"
#include "psprecomp/common.hpp"
#include "savedata_host.hpp"
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iostream>
#include <vector>
namespace ff3 { namespace {
enum class UtilityStatus : std::uint32_t {
    None = 0u,
    Init = 1u,
    Visible = 2u,
    Quit = 3u,
    Finished = 4u,
};

struct SavedataUtilityState {
    UtilityStatus status{UtilityStatus::None};
    std::uint32_t parameter_address{};
    bool operation_complete{};
};

SavedataUtilityState savedata_utility{};
SaveSlotHandler slot_handler;

constexpr std::uint32_t kUtilityCommonResultOffset = 0x1Cu;
constexpr std::uint32_t kSavedataModeOffset = 0x30u;
constexpr std::uint32_t kSavedataGameNameOffset = 0x3Cu;
constexpr std::uint32_t kSavedataSaveNameOffset = 0x4Cu;
constexpr std::uint32_t kSavedataFileNameOffset = 0x64u;
constexpr std::uint32_t kSavedataDataBufferOffset = 0x74u;
constexpr std::uint32_t kSavedataDataBufferSizeOffset = 0x78u;
constexpr std::uint32_t kSavedataDataSizeOffset = 0x7Cu;
constexpr std::uint32_t kSavedataIcon0Offset = 0x584u;
constexpr std::uint32_t kSavedataIcon1Offset = 0x594u;
constexpr std::uint32_t kSavedataPic1Offset = 0x5A4u;
constexpr std::uint32_t kSavedataSnd0Offset = 0x5B4u;
constexpr std::uint32_t kSavedataIdListOffset = 0x5F4u;
constexpr std::uint32_t kSavedataFileListOffset = 0x5F8u;
constexpr std::uint32_t kSavedataSizeInfoOffset = 0x5FCu;
constexpr std::uint32_t kSavedataParameterMinimumSize = 0x600u;

std::string read_fixed_string(const psprecomp::GuestMemory &memory, std::uint32_t address, std::size_t size) {
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const char value = static_cast<char>(memory.load8(address + static_cast<std::uint32_t>(index)));
        if (value == '\0') break;
        result.push_back(value);
    }
    return result;
}

std::string safe_savedata_component(std::string value) {
    if(value=="." || value==".." || value.find_first_of("/\\:")!=std::string::npos)
        throw psprecomp::Error("Invalid savedata path component");
    return value;
}

std::filesystem::path local_save_root;
std::filesystem::path savedata_root(const psprecomp::Runtime &) { return local_save_root; }

std::filesystem::path savedata_directory(const psprecomp::Runtime &runtime, std::uint32_t parameter_address) {
    const std::string game = safe_savedata_component(read_fixed_string(
        runtime.memory(), parameter_address + kSavedataGameNameOffset, 13u));
    const std::string save = safe_savedata_component(read_fixed_string(
        runtime.memory(), parameter_address + kSavedataSaveNameOffset, 20u));
    auto root=std::filesystem::weakly_canonical(savedata_root(runtime));
    auto result=std::filesystem::weakly_canonical(root / (game+save));
    if(result.parent_path()!=root) throw psprecomp::Error("Savedata path escapes memory stick");
    return result;
}

bool write_guest_file(psprecomp::Runtime &runtime, const std::filesystem::path &path,
                      std::uint32_t buffer, std::uint32_t size) {
    if (size != 0u && (buffer == 0u || !runtime.memory().contains(buffer, size))) return false;
    std::vector<std::uint8_t> data(size);
    for (std::uint32_t index = 0u; index < size; ++index)
        data[index] = runtime.memory().load8(buffer + index);
    std::filesystem::create_directories(path.parent_path());
    // Stage beside the destination so rename replaces a complete file on the
    // same filesystem. Never truncate a player's existing save in place.
    static std::uint64_t sequence{};
    auto staging = path.parent_path() / (".ff3-save-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(++sequence));
    if (!std::filesystem::create_directory(staging)) return false;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{staging};
    const auto temporary = staging / "data";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    if (!data.empty()) output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output) return false;
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
}

bool write_savedata_auxiliary(psprecomp::Runtime &runtime, std::uint32_t parameter_address,
                              std::uint32_t descriptor_offset, const char *filename) {
    const std::uint32_t descriptor = parameter_address + descriptor_offset;
    const std::uint32_t buffer = runtime.memory().load32(descriptor);
    const std::uint32_t buffer_size = runtime.memory().load32(descriptor + 4u);
    const std::uint32_t actual_size = runtime.memory().load32(descriptor + 8u);
    if (buffer == 0u || actual_size == 0u) return true;
    if (actual_size > buffer_size) return false;
    return write_guest_file(runtime, savedata_directory(runtime, parameter_address) / filename, buffer, actual_size);
}

std::uint32_t load_savedata_file(psprecomp::Runtime &runtime, std::uint32_t parameter_address,
                                 const std::filesystem::path &path, bool raw_mode) {
    if (!std::filesystem::is_regular_file(path)) {
        return raw_mode ? 0x80110329u : 0x80110307u;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return raw_mode ? 0x80110329u : 0x80110305u;
    const auto end = input.tellg();
    if (end < 0) return 0x80110305u;
    const auto file_size = static_cast<std::uint64_t>(end);
    const std::uint32_t destination = runtime.memory().load32(parameter_address + kSavedataDataBufferOffset);
    const std::uint32_t capacity = runtime.memory().load32(parameter_address + kSavedataDataBufferSizeOffset);
    if (file_size > capacity || file_size > 0xFFFFFFFFull ||
        (file_size != 0u && (destination == 0u || !runtime.memory().contains(destination, static_cast<std::size_t>(file_size))))) {
        return raw_mode ? 0x80110328u : 0x80110308u;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) return 0x80110305u;
    if (!bytes.empty()) runtime.memory().copy_in(destination, bytes);
    runtime.memory().store32(parameter_address + kSavedataDataSizeOffset, static_cast<std::uint32_t>(bytes.size()));
    return 0u;
}

std::uint32_t save_savedata_file(psprecomp::Runtime &runtime, std::uint32_t parameter_address,
                                 const std::filesystem::path &path, bool raw_mode) {
    const std::uint32_t source = runtime.memory().load32(parameter_address + kSavedataDataBufferOffset);
    const std::uint32_t capacity = runtime.memory().load32(parameter_address + kSavedataDataBufferSizeOffset);
    const std::uint32_t size = runtime.memory().load32(parameter_address + kSavedataDataSizeOffset);
    if (size > capacity || (size != 0u && (source == 0u || !runtime.memory().contains(source, size)))) {
        return raw_mode ? 0x80110328u : 0x80110388u;
    }
    if (!raw_mode) {
        if (!write_savedata_auxiliary(runtime, parameter_address, kSavedataIcon0Offset, "ICON0.PNG") ||
            !write_savedata_auxiliary(runtime, parameter_address, kSavedataIcon1Offset, "ICON1.PMF") ||
            !write_savedata_auxiliary(runtime, parameter_address, kSavedataPic1Offset, "PIC1.PNG") ||
            !write_savedata_auxiliary(runtime, parameter_address, kSavedataSnd0Offset, "SND0.AT3")) {
            return 0x80110385u;
        }
    }
    if (!write_guest_file(runtime, path, source, size)) return raw_mode ? 0x80110329u : 0x80110385u;
    return 0u;
}

// PSP savedata LIST uses '*' and '?' in the save-name suffix.
bool savedata_name_matches(std::string_view pattern, std::string_view name) {
    std::size_t p=0, n=0, star=std::string_view::npos, retry=0;
    while (n<name.size()) {
        if (p<pattern.size() && (pattern[p]=='?' || pattern[p]==name[n])) {++p;++n;}
        else if (p<pattern.size() && pattern[p]=='*') {star=p++;retry=n;}
        else if (star!=std::string_view::npos) {p=star+1;n=++retry;}
        else return false;
    }
    while (p<pattern.size() && pattern[p]=='*') ++p;
    return p==pattern.size();
}

std::uint32_t list_savedata_directories(psprecomp::Runtime &runtime, std::uint32_t parameter_address) {
    const std::uint32_t info = runtime.memory().load32(parameter_address + kSavedataIdListOffset);
    if (info == 0u || !runtime.memory().contains(info, 12u)) return 0x80110328u;
    const std::int32_t max_count = static_cast<std::int32_t>(runtime.memory().load32(info));
    const std::uint32_t entries = runtime.memory().load32(info + 8u);
    if (max_count < 0 || (max_count > 0 && (entries == 0u || !runtime.memory().contains(entries, static_cast<std::size_t>(max_count) * 72u)))) {
        return 0x80110328u;
    }
    const std::string game = safe_savedata_component(read_fixed_string(
        runtime.memory(), parameter_address + kSavedataGameNameOffset, 13u));
    const std::string pattern = read_fixed_string(runtime.memory(),
        parameter_address + kSavedataSaveNameOffset, 20u);
    std::vector<std::string> names;
    const auto root = savedata_root(runtime);
    if (std::filesystem::is_directory(root)) {
        for (const auto &entry : std::filesystem::directory_iterator(root)) {
            if (entry.is_symlink() || !entry.is_directory()) continue;
            const std::string directory_name = entry.path().filename().string();
            if (!directory_name.starts_with(game)) continue;
            const auto name=directory_name.substr(game.size());
            if (name.size()>19u || !savedata_name_matches(pattern,name)) continue;
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    if (names.size() > static_cast<std::size_t>(max_count)) names.resize(static_cast<std::size_t>(max_count));
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::uint32_t entry = entries + static_cast<std::uint32_t>(index * 72u);
        runtime.memory().zero(entry, 72u);
        runtime.memory().store32(entry, 0x11FFu);
        std::vector<std::uint8_t> bytes(names[index].begin(), names[index].end());
        if (bytes.size() > 19u) bytes.resize(19u);
        bytes.push_back(0u);
        runtime.memory().copy_in(entry + 52u, bytes);
    }
    runtime.memory().store32(info + 4u, static_cast<std::uint32_t>(names.size()));
    return 0u;
}

std::uint64_t directory_size_bytes(const std::filesystem::path &directory) {
    std::uint64_t total = 0u;
    if (!std::filesystem::is_directory(directory)) return total;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(directory, error), end; it != end && !error; it.increment(error)) {
        if (it->is_regular_file(error)) total += it->file_size(error);
    }
    return total;
}

void write_small_size_string(psprecomp::GuestMemory &memory, std::uint32_t address, std::uint64_t kilobytes) {
    const std::string text = kilobytes > 99999u ? "99999KB" : std::to_string(kilobytes) + "KB";
    memory.zero(address, 8u);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    if (bytes.size() > 7u) bytes.resize(7u);
    bytes.push_back(0u);
    memory.copy_in(address, bytes);
}

void write_used_data_info(psprecomp::GuestMemory &memory, std::uint32_t address,
                          std::uint64_t used_bytes, std::uint32_t cluster_size) {
    const std::uint64_t clusters = (used_bytes + cluster_size - 1u) / cluster_size;
    const std::uint64_t used_kb = (used_bytes + 1023u) / 1024u;
    const std::uint64_t used_32kb = clusters * (cluster_size / 1024u);
    memory.store32(address + 0u, static_cast<std::uint32_t>(std::min<std::uint64_t>(clusters, 0xFFFFFFFFull)));
    memory.store32(address + 4u, static_cast<std::uint32_t>(std::min<std::uint64_t>(used_kb, 0xFFFFFFFFull)));
    write_small_size_string(memory, address + 8u, used_kb);
    memory.store32(address + 16u, static_cast<std::uint32_t>(std::min<std::uint64_t>(used_32kb, 0xFFFFFFFFull)));
    write_small_size_string(memory, address + 20u, used_32kb);
}

std::uint32_t query_savedata_sizes(psprecomp::Runtime &runtime, std::uint32_t parameter_address) {
    constexpr std::uint32_t cluster_size = 32u * 1024u;
    const auto root = savedata_root(runtime);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto space = std::filesystem::space(root, error);
    const std::uint64_t available = error ? 512ull * 1024ull * 1024ull : space.available;
    const std::uint64_t free_clusters = available / cluster_size;
    const std::uint64_t free_kb = available / 1024u;
    const std::uint64_t used = directory_size_bytes(savedata_directory(runtime, parameter_address));

    const std::uint32_t ms_free = runtime.memory().load32(parameter_address + 0x5D0u);
    if (ms_free != 0u) {
        if (!runtime.memory().contains(ms_free, 20u)) return 0x801103C8u;
        runtime.memory().store32(ms_free + 0u, cluster_size);
        runtime.memory().store32(ms_free + 4u, static_cast<std::uint32_t>(std::min<std::uint64_t>(free_clusters, 0xFFFFFFFFull)));
        runtime.memory().store32(ms_free + 8u, static_cast<std::uint32_t>(std::min<std::uint64_t>(free_kb, 0xFFFFFFFFull)));
        write_small_size_string(runtime.memory(), ms_free + 12u, free_kb);
    }

    const std::uint32_t ms_data = runtime.memory().load32(parameter_address + 0x5D4u);
    if (ms_data != 0u) {
        if (!runtime.memory().contains(ms_data, 64u)) return 0x801103C8u;
        runtime.memory().zero(ms_data, 64u);
        for (std::uint32_t index = 0u; index < 13u; ++index)
            runtime.memory().store8(ms_data + index, runtime.memory().load8(parameter_address + kSavedataGameNameOffset + index));
        for (std::uint32_t index = 0u; index < 20u; ++index)
            runtime.memory().store8(ms_data + 16u + index, runtime.memory().load8(parameter_address + kSavedataSaveNameOffset + index));
        write_used_data_info(runtime.memory(), ms_data + 36u, used, cluster_size);
    }

    const std::uint32_t utility_data = runtime.memory().load32(parameter_address + 0x5D8u);
    if (utility_data != 0u) {
        if (!runtime.memory().contains(utility_data, 28u)) return 0x801103C8u;
        write_used_data_info(runtime.memory(), utility_data, used, cluster_size);
    }
    return 0u;
}

std::uint32_t query_savedata_capacity(psprecomp::Runtime &rt,std::uint32_t p) {
    auto &m=rt.memory();const auto info=m.load32(p+0x5FC);
    if(!m.contains(info,60))return 0x80110328;
    constexpr std::uint64_t cluster=32768;
    std::uint64_t required=cluster; // directory/SFO allocation
    for(unsigned kind=0;kind<2;++kind){
        const auto count=m.load32(info+4*kind),entries=m.load32(info+8+4*kind);
        if(count>4096 || (count&&!m.contains(entries,24ull*count)))return 0x80110328;
        for(unsigned i=0;i<count;++i){
            const std::uint64_t size=m.load32(entries+24*i)|(static_cast<std::uint64_t>(m.load32(entries+24*i+4))<<32);
            if(size>0xFFFFFFFFull)return 0x80110328;
            required+=((size+cluster-1)/cluster)*cluster;
        }
    }
    std::error_code error;std::filesystem::create_directories(local_save_root,error);
    if(error)return 0x80110323;
    auto space=std::filesystem::space(local_save_root,error);if(error)return 0x80110323;
    auto existing=directory_size_bytes(savedata_directory(rt,p));
    auto need=required>space.available?(required-space.available+1023)/1024:0;
    auto overwrite=required>space.available+existing?(required-space.available-existing+1023)/1024:0;
    m.store32(info+16,cluster);m.store32(info+20,std::min<std::uint64_t>(space.available/cluster,0x7FFFFFFF));
    m.store32(info+24,std::min<std::uint64_t>(space.available/1024,0x7FFFFFFF));write_small_size_string(m,info+28,space.available/1024);
    m.store32(info+36,need);write_small_size_string(m,info+40,need);
    m.store32(info+48,overwrite);write_small_size_string(m,info+52,overwrite);
    return 0;
}

std::uint32_t execute_savedata_operation(psprecomp::Runtime &runtime, std::uint32_t parameter_address) {
    const std::uint32_t mode = runtime.memory().load32(parameter_address + kSavedataModeOffset);
    const std::string file_name_value = safe_savedata_component(read_fixed_string(
        runtime.memory(), parameter_address + kSavedataFileNameOffset, 13u));
    const std::string file_name = file_name_value.empty() ? "DATA.BIN" : file_name_value;
    if (mode == 4u || mode == 5u) {
        auto &memory = runtime.memory();
        const auto list = memory.load32(parameter_address + 0x60u);
        if (list == 0u) return 0x80110328u;
        const auto game = safe_savedata_component(read_fixed_string(memory, parameter_address + kSavedataGameNameOffset, 13u));
        std::vector<SaveSlot> slots;
        bool terminated = false;
        for (std::uint32_t i = 0; i < 1024u; ++i) {
            const auto address = static_cast<std::uint64_t>(list) + i * 20u;
            if (address > 0xFFFFFFFFull || !memory.contains(static_cast<std::uint32_t>(address), 20u)) return 0x80110328u;
            const auto name = safe_savedata_component(read_fixed_string(memory, static_cast<std::uint32_t>(address), 20u));
            if (name.empty()) { terminated = true; break; }
            if (name.size() >= 20u) return 0x80110328u;
            const bool occupied = std::filesystem::is_regular_file(savedata_root(runtime) / (game + name) / file_name);
            if (mode == 5u || occupied) slots.push_back({name, occupied});
        }
        if (!terminated) return 0x80110328u;
        if (slots.empty()) return mode == 4u ? 0x80110307u : 0x80110301u;
        // A list request needs a real selection; never silently save into the
        // empty suffix used by FF3's separate SYSTEM.BIN file.
        if (!slot_handler) return 0x80110301u;
        const auto selected = slot_handler(slots, mode == 5u);
        if (!selected) return 0x80110301u;
        if (*selected >= slots.size()) return 0x80110328u;
        memory.zero(parameter_address + kSavedataSaveNameOffset, 20u);
        const auto &name = slots[*selected].name;
        for (std::size_t i = 0; i < name.size(); ++i)
            memory.store8(parameter_address + kSavedataSaveNameOffset + static_cast<std::uint32_t>(i), name[i]);
    }
    const auto directory = savedata_directory(runtime, parameter_address);
    const auto data_path = std::filesystem::weakly_canonical(directory / file_name);
    if(data_path.parent_path()!=directory) throw psprecomp::Error("Savedata file escapes save directory");
    switch (mode) {
    case 0u: // AUTOLOAD
    case 2u: // LOAD
    case 4u: // LISTLOAD
        return load_savedata_file(runtime, parameter_address, data_path, false);
    case 1u: // AUTOSAVE
    case 3u: // SAVE
    case 5u: // LISTSAVE
        return save_savedata_file(runtime, parameter_address, data_path, false);
    case 9u: // AUTODELETE
    case 10u: // DELETE
        if (!std::filesystem::exists(directory)) return 0x80110347u;
        return std::filesystem::remove_all(directory) != 0u ? 0u : 0x80110345u;
    case 11u: // LIST
        return list_savedata_directories(runtime, parameter_address);
    case 13u: // MAKEDATASECURE
    case 14u: // MAKEDATA
    case 17u: // WRITEDATASECURE
    case 18u: // WRITEDATA
        return save_savedata_file(runtime, parameter_address, data_path, true);
    case 15u: // READDATASECURE
    case 16u: // READDATA
        return load_savedata_file(runtime, parameter_address, data_path, true);
    case 19u: // ERASESECURE
    case 20u: // ERASE
    case 21u: // DELETEDATA
        if (!std::filesystem::is_regular_file(data_path)) return 0x80110329u;
        return std::filesystem::remove(data_path) ? 0u : 0x80110329u;
    case 8u:  // SIZES
        return query_savedata_sizes(runtime, parameter_address);
    case 22u: return query_savedata_capacity(runtime,parameter_address);
    case 12u: // FILES
        runtime.stop("Savedata query mode not yet implemented: "+std::to_string(mode)); return 0x80110300;
    default:
        return 0x80110300u;
    }
}


}
void set_save_slot_handler(SaveSlotHandler handler) { slot_handler = std::move(handler); }
void install_savedata(psprecomp::Runtime &runtime,const std::filesystem::path &root){
local_save_root=root;
savedata_utility={};
slot_handler={};
    runtime.register_hle("sceUtility", 0x50C4CD57u,
        [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const std::uint32_t parameter = ctx.gpr[4];
            if (savedata_utility.status != UtilityStatus::None) {
                ctx.set_gpr(2, 0x80110001u);
                return;
            }
            if (parameter == 0u || !rt.memory().contains(parameter, 4u)) {
                ctx.set_gpr(2, 0x80110004u);
                return;
            }
            const std::uint32_t declared_size = rt.memory().load32(parameter);
            if (declared_size < 0x5C0u || !rt.memory().contains(parameter, std::min<std::uint32_t>(declared_size, kSavedataParameterMinimumSize))) {
                ctx.set_gpr(2, 0x80110004u);
                return;
            }
            savedata_utility = SavedataUtilityState{UtilityStatus::Init, parameter, false};
            rt.memory().store32(parameter + kUtilityCommonResultOffset, 0u);
            if (true) {
                std::cerr << "[hle] savedata init mode=" << rt.memory().load32(parameter + kSavedataModeOffset)
                          << " game=" << read_fixed_string(rt.memory(), parameter + kSavedataGameNameOffset, 13u)
                          << " save=" << read_fixed_string(rt.memory(), parameter + kSavedataSaveNameOffset, 20u)
                          << " file=" << read_fixed_string(rt.memory(), parameter + kSavedataFileNameOffset, 13u)
                          << " size=0x" << std::hex << declared_size << std::dec << "\n";
            }
            ctx.set_gpr(2,0);
        });
    runtime.register_hle("sceUtility", 0xD4B95FFBu,
        [](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            if (savedata_utility.status == UtilityStatus::None || savedata_utility.status == UtilityStatus::Finished) {
                ctx.set_gpr(2, 0x80110001u);
                return;
            }
            if (savedata_utility.status == UtilityStatus::Init) {
                savedata_utility.status = UtilityStatus::Visible;
            } else if (savedata_utility.status == UtilityStatus::Visible && !savedata_utility.operation_complete) {
                std::uint32_t result{};
                try {
                    result = execute_savedata_operation(rt, savedata_utility.parameter_address);
                } catch (const std::filesystem::filesystem_error &) {
                    const auto mode=rt.memory().load32(savedata_utility.parameter_address+kSavedataModeOffset);
                    result=mode>=11u?0x80110329u:(mode==1u||mode==3u||mode==5u?0x80110385u:0x80110305u);
                } catch (const psprecomp::Error &) {
                    result=0x80110328u;
                }
                rt.memory().store32(savedata_utility.parameter_address + kUtilityCommonResultOffset, result);
                savedata_utility.operation_complete = true;
                savedata_utility.status = UtilityStatus::Quit;
                if (true) {
                    std::cerr << "[hle] savedata operation result=0x" << std::hex << std::uppercase << result
                              << std::nouppercase << std::dec << "\n";
                }
            }
            ctx.set_gpr(2,0);
        });
    runtime.register_hle("sceUtility", 0x8874DBE0u,
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const UtilityStatus reported = savedata_utility.status;
            ctx.set_gpr(2, static_cast<std::uint32_t>(reported));
            if (reported == UtilityStatus::Init) {
                // PSP utility initialization completes on its own access thread.
                // Expose INIT once, then make the dialog visible for Update().
                savedata_utility.status = UtilityStatus::Visible;
            } else if (reported == UtilityStatus::Finished) {
                savedata_utility = SavedataUtilityState{};
            }
        });
    runtime.register_hle("sceUtility", 0x9790B33Cu,
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            if (savedata_utility.status != UtilityStatus::Quit) {
                ctx.set_gpr(2, 0x80110001u);
                return;
            }
            savedata_utility.status = UtilityStatus::Finished;
            ctx.set_gpr(2,0);
        });

}
}
