cmake_minimum_required(VERSION 3.21)
project(UNFV3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

# Ensure RPATH is set for the bundle
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
endif()

# --------------------------------------------------
# Global build type default
# --------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(UNFV3_ENABLE_LTO "Enable link-time optimization for Release builds" ON)
option(UNFV3_AGGRESSIVE_STRIP "Remove RTTI and Exceptions" OFF)
option(UNFV3_USE_SYSTEM_OPENCV "Use system OpenCV (may pull shared libs at runtime)" OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(ipo_supported AND UNFV3_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "LTO enabled")
endif()

# ThinLTO for Clang
if(UNFV3_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<CONFIG:Release>:-flto=thin>)
    add_link_options($<$<CONFIG:Release>:-flto=thin>)
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
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-O3>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-ffunction-sections>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-fdata-sections>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:RelWithDebInfo>>:-O2>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:MinSizeRel>>:-Os>

    # MSVC — speed optimisation
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>>:/O2>
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>>:/Gy>   # function-level linking
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>>:/Gw>   # data-section splitting

    # MSVC whole-program opt — only when LTO is explicitly requested
    $<$<AND:${_c_or_cxx},${_msvc},$<CONFIG:Release>,$<BOOL:${UNFV3_ENABLE_LTO}>>:/GL>

    # Aggressive stripping: No RTTI, No Exceptions
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<BOOL:${UNFV3_AGGRESSIVE_STRIP}>>:-fno-rtti>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<BOOL:${UNFV3_AGGRESSIVE_STRIP}>>:-fno-exceptions>
    $<$<AND:${_c_or_cxx},${_msvc},$<BOOL:${UNFV3_AGGRESSIVE_STRIP}>>:/GR->
    $<$<AND:${_c_or_cxx},${_msvc},$<BOOL:${UNFV3_AGGRESSIVE_STRIP}>>:/EHs-c->
)

target_link_options(unfv3_project_optimizations INTERFACE
    # MSVC link-time code generation — only when LTO is explicitly requested
    $<$<AND:${_msvc},$<CONFIG:Release>,$<BOOL:${UNFV3_ENABLE_LTO}>>:/LTCG>
    $<$<AND:${_msvc},$<CONFIG:Release>>:/OPT:REF>
    $<$<AND:${_msvc},$<CONFIG:Release>>:/OPT:ICF>

    # Linker Garbage Collection (Linux)
    $<$<AND:${_c_or_cxx},$<PLATFORM_ID:Linux>,${_gnu_clang},$<CONFIG:Release>>:-Wl,--gc-sections>

    # Linker Garbage Collection (macOS)
    $<$<AND:${_c_or_cxx},$<PLATFORM_ID:Darwin>,${_gnu_clang},$<CONFIG:Release>>:-Wl,-dead_strip>
)

# --------------------------------------------------
# SDL3
# --------------------------------------------------

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL)

# --------------------------------------------------
# ncnn (portable, no Vulkan)
# --------------------------------------------------

set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)

add_subdirectory(vendored/ncnn)

# --------------------------------------------------
# GEGL (graph-based image processing)
# --------------------------------------------------

find_package(PkgConfig REQUIRED)
# Add IMPORTED_TARGET to these lines
pkg_check_modules(GEGL REQUIRED IMPORTED_TARGET gegl-0.4)
pkg_check_modules(BABL REQUIRED IMPORTED_TARGET babl-0.1)

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
    PkgConfig::GEGL 
    PkgConfig::BABL
)

if(WIN32)
    target_link_libraries(UNFV3 PRIVATE ws2_32 comctl32 setupapi version)
endif()

# --------------------------------------------------
# Post-build stripping
# --------------------------------------------------

if(NOT APPLE AND NOT WIN32)
    add_custom_command(TARGET UNFV3 POST_BUILD
        COMMAND strip --strip-unneeded $<TARGET_FILE:UNFV3>
        COMMENT "Stripping UNFV3 (Linux)"
    )
elseif(APPLE)
    add_custom_command(TARGET UNFV3 POST_BUILD
        COMMAND strip -x $<TARGET_FILE:UNFV3>
        COMMENT "Stripping UNFV3 (macOS)"
    )
elseif(WIN32)
    add_custom_command(TARGET UNFV3 POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:UNFV3>
        COMMENT "Stripping UNFV3 (Windows)"
    )
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

# --------------------------------------------------
# Bundle & Distribution (V3 System Implementation)
# --------------------------------------------------

include(bundle_tools.cmake)

if(NOT WIN32)
    set(BUNDLE_DEST "UNFV3_bundle")
else()
    set(BUNDLE_DEST "UNFV3")
endif()

bundle_runtime_dependencies(UNFV3 "${BUNDLE_DEST}")

# Install main executable to bundle root
install(TARGETS UNFV3 DESTINATION "${BUNDLE_DEST}")

# Install Data directory
install(DIRECTORY Data DESTINATION "${BUNDLE_DEST}")
