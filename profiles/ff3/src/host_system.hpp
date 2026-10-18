#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace ff3 {

inline bool local_calendar_time(std::time_t time, std::tm &date) {
#if defined(_WIN32)
    return localtime_s(&date, &time) == 0;
#else
    return localtime_r(&time, &date) != nullptr;
#endif
}

inline std::FILE *open_file(const std::filesystem::path &path, const char *mode) {
#if defined(_WIN32)
    wchar_t wide_mode[8]{};
    for (std::size_t index = 0; mode[index] != '\0' && index + 1 < sizeof wide_mode; ++index)
        wide_mode[index] = static_cast<wchar_t>(static_cast<unsigned char>(mode[index]));
    return _wfopen(path.c_str(), wide_mode);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

inline int seek_file(std::FILE *stream, std::int64_t offset, int origin) {
#if defined(_WIN32)
    return _fseeki64(stream, offset, origin);
#else
    return fseeko(stream, static_cast<off_t>(offset), origin);
#endif
}

inline std::int64_t tell_file(std::FILE *stream) {
#if defined(_WIN32)
    return _ftelli64(stream);
#else
    return static_cast<std::int64_t>(ftello(stream));
#endif
}

inline void set_environment(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

inline void unset_environment(const char *name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}
