cmake_minimum_required(VERSION 3.20)
project(UNFV3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

# --------------------------------------------------
# Global build type default
# --------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(UNFV3_ENABLE_LTO "Enable link-time optimization for Release builds" OFF)
option(UNFV3_USE_SYSTEM_OPENCV "Use system OpenCV (may pull shared libs at runtime)" OFF)

if(UNFV3_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# --------------------------------------------------
# Platform: Windows static-runtime + no DLLs
# --------------------------------------------------

if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    # Static CRT: MT in Release, MTd in Debug.
    # This is consumed by CMake's MSVC toolchain; no linker flag needed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX VC_EXTRALEAN)
endif()

# --------------------------------------------------
# Project optimization profile (applies only to our targets)
# --------------------------------------------------

set(_gnu_clang "$<CXX_COMPILER_ID:GNU,Clang>")
set(_msvc      "$<CXX_COMPILER_ID:MSVC>")
set(_c_or_cxx  "$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>")

add_library(unfv3_project_optimizations INTERFACE)

target_compile_options(unfv3_project_optimizations INTERFACE
    # GCC / Clang — omit frame pointer in all build types
    $<$<AND:${_c_or_cxx},${_gnu_clang}>:-fomit-frame-pointer>

    # GCC / Clang — optimisation level per config
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-O2>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:RelWithDebInfo>>:-O2>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:MinSizeRel>>:-Os>

    # MSVC — speed optimisation
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>>:/O2>
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>>:/Gy>   # function-level linking
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>>:/Gw>   # data-section splitting

    # MSVC whole-program opt — only when LTO is explicitly requested
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>,$<BOOL:${UNFV3_ENABLE_LTO}>>:/GL>
)

target_link_options(unfv3_project_optimizations INTERFACE
    # MSVC link-time code generation — only when LTO is explicitly requested
    $<$<AND:${_msvc},$<CONFIG:Release>,$<BOOL:${UNFV3_ENABLE_LTO}>>:/LTCG>
    $<$<AND:${_msvc},$<CONFIG:Release>>:/OPT:REF>
    $<$<AND:${_msvc},$<CONFIG:Release>>:/OPT:ICF>
)

# --------------------------------------------------
# SDL3
# --------------------------------------------------

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL)

# --------------------------------------------------
# ncnn (portable, no Vulkan)
# --------------------------------------------------

set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)

add_subdirectory(vendored/ncnn)

# --------------------------------------------------
# GEGL (graph-based image processing)
# --------------------------------------------------

option(UNFV3_USE_SYSTEM_GEGL "Use system-installed GEGL" ON)

if(UNFV3_USE_SYSTEM_GEGL)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(GEGL REQUIRED gegl-0.4)

    message(STATUS "Found GEGL via pkg-config")

    set(GEGL_INCLUDE_DIRS ${GEGL_INCLUDE_DIRS})
    set(GEGL_LIBS ${GEGL_LIBRARIES})
else()
    message(FATAL_ERROR "Vendored GEGL not implemented (yet). Use system GEGL.")
endif()

# --------------------------------------------------
# Tracy Profiler
# --------------------------------------------------

option(UNFV3_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)

if(UNFV3_ENABLE_TRACY)
    include(FetchContent)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.13.1
    )
    FetchContent_MakeAvailable(tracy)
endif()

# --------------------------------------------------
# ImGui
# --------------------------------------------------

add_library(imgui STATIC
    vendored/imgui/imgui.cpp
    vendored/imgui/imgui_draw.cpp
    vendored/imgui/imgui_tables.cpp
    vendored/imgui/imgui_widgets.cpp
    vendored/imgui/imgui_demo.cpp
    vendored/imgui/backends/imgui_impl_sdl3.cpp
    vendored/imgui/backends/imgui_impl_sdlgpu3.cpp
    vendored/imgui/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui PUBLIC
    vendored/imgui
    vendored/imgui/backends
    vendored/SDL/include
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --------------------------------------------------
# SQLite
# --------------------------------------------------

add_library(sqlite STATIC
    vendored/sqlite-amalgamation-3510200/sqlite3.c
)

target_include_directories(sqlite PUBLIC
    vendored/sqlite-amalgamation-3510200
)

target_compile_definitions(sqlite PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_RTREE
    SQLITE_THREADSAFE=2
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_MAX_EXPR_DEPTH=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_USE_ALLOCA
)

target_link_libraries(sqlite PRIVATE unfv3_project_optimizations)

# --------------------------------------------------
# nlohmann_json (header-only)
# --------------------------------------------------

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE vendored/json)

# --------------------------------------------------
# Main executable
# --------------------------------------------------

add_executable(UNFV3
    src/main.cpp
    src/Application/database.cpp
    src/Application/session.cpp
    src/Application/search_engine.cpp
    src/Application/detection.cpp
    src/Application/ui.cpp
    src/Application/image_editor.cpp
    src/Application/image.cpp
    src/Application/gpu_utils.cpp
    src/Application/operations/gimp_levels.cpp
    src/Application/operations/my_colour_enhance.cpp
)

target_include_directories(UNFV3 PRIVATE src/Application)
target_include_directories(UNFV3 PRIVATE ${GEGL_INCLUDE_DIRS})

target_link_libraries(UNFV3 PRIVATE
    imgui
    SDL3::SDL3
    sqlite
    nlohmann_json
    unfv3_project_optimizations
    ncnn
    ${GEGL_LIBS}
)

if(WIN32)
    target_link_libraries(UNFV3 PRIVATE ws2_32 comctl32 setupapi version)
endif()

# --------------------------------------------------
# Tracy
# --------------------------------------------------

if(UNFV3_ENABLE_TRACY)
    target_link_libraries(UNFV3 PRIVATE TracyClient)
    target_compile_definitions(UNFV3 PRIVATE TRACY_ENABLE)
endif()

# --------------------------------------------------
# Diagnostics
# --------------------------------------------------

message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "LTO enabled      : ${UNFV3_ENABLE_LTO}")
message(STATUS "GEGL libs        : ${GEGL_LIBS}")
