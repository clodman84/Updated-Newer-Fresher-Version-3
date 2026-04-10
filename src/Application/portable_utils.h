#ifndef PORTABLE_UTILS_H
#define PORTABLE_UTILS_H

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <vector>

#ifdef WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

inline std::filesystem::path get_executable_path() {
#ifdef WIN32
    std::vector<wchar_t> path(MAX_PATH);
    DWORD size = GetModuleFileNameW(NULL, path.data(), (DWORD)path.size());
    while (size == path.size()) {
        path.resize(path.size() * 2);
        size = GetModuleFileNameW(NULL, path.data(), (DWORD)path.size());
    }
    return std::filesystem::path(std::wstring(path.data(), size)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0) {
        return std::filesystem::path(path.data()).parent_path();
    }
    return std::filesystem::path(".");
#else
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count > 0) {
        return std::filesystem::path(std::string(result, count)).parent_path();
    }
    return std::filesystem::path(".");
#endif
}

inline void setup_portable_paths() {
    auto base_path = get_executable_path();

    auto gegl_path = (base_path / "lib" / "gegl-0.4").string();
    auto babl_path = (base_path / "lib" / "babl-0.1").string();

#ifdef WIN32
    _putenv_s("GEGL_PATH", gegl_path.c_str());
    _putenv_s("BABL_PATH", babl_path.c_str());
#else
    setenv("GEGL_PATH", gegl_path.c_str(), 1);
    setenv("BABL_PATH", babl_path.c_str(), 1);
#endif

    if (const char* trace = std::getenv("UNFV3_TRACE_GEGL")) {
        std::cout << "[Portable] Executable path: " << base_path << std::endl;
        std::cout << "[Portable] Setting GEGL_PATH to: " << gegl_path << std::endl;
        std::cout << "[Portable] Setting BABL_PATH to: " << babl_path << std::endl;
    }
}

#endif
