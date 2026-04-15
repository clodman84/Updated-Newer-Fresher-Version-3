cmake_minimum_required(VERSION 3.21)
project(UNFV3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# set(BUILD_SHARED_LIBS OFF)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)

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

find_package(SDL3 REQUIRED)
find_package(ncnn REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GEGL REQUIRED IMPORTED_TARGET gegl-0.4)
pkg_check_modules(BABL REQUIRED IMPORTED_TARGET babl-0.1)

if(UNFV3_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

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
    unfv3_project_optimizations
    imgui::imgui                 
    SDL3::SDL3                  
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    ncnn                       
    PkgConfig::GEGL           
    PkgConfig::BABL          
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)

if(UNFV3_ENABLE_TRACY)
    target_link_libraries(UNFV3 PRIVATE Tracy::TracyClient) # Changed from TracyClient
    target_compile_definitions(UNFV3 PRIVATE TRACY_ENABLE)
endif()

if(WIN32)
    target_link_libraries(UNFV3 PRIVATE ws2_32 comctl32 setupapi version)
endif()

# --------------------------------------------------
# Post-build
# --------------------------------------------------

# 1. Setup paths
set(BUNDLE_LIB_DIR "$<TARGET_FILE_DIR:UNFV3>/lib")
set(VCPKG_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")

# 2. Copy the plugin folders (Existing logic, ensure paths are absolute)
add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_LIB_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VCPKG_LIB_DIR}/babl-0.1" "${BUNDLE_LIB_DIR}/babl-0.1"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VCPKG_LIB_DIR}/gegl-0.4" "${BUNDLE_LIB_DIR}/gegl-0.4"
    COMMENT "Bundling plugin folders"
)

# 3. Copy ALL .so files from vcpkg to your bundle's lib/
# We use -P to preserve symlinks for versioned libs like libTracyClient.so.0.13.1
add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND /bin/sh -c "cp -Pn ${VCPKG_LIB_DIR}/*.so* ${BUNDLE_LIB_DIR}/"
    COMMENT "Copying shared libraries to bundle"
)

# 4. Patch the Main Executable to look in ./lib
add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND patchelf --set-rpath "\\$ORIGIN/lib" $<TARGET_FILE:UNFV3>
    COMMENT "Directing UNFV3 to look in local lib folder"
)

# 5. Patch the Plugins to look at their parent folder (lib/)
add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND /bin/sh -c "find ${BUNDLE_LIB_DIR} -name '*.so*' -exec patchelf --set-rpath '\\$ORIGIN:\\$ORIGIN/..' {} +"
    COMMENT "Directing plugins to look in parent lib folder"
)

# 6. Your existing Strip logic
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
# Diagnostics
# --------------------------------------------------

message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "LTO enabled      : ${UNFV3_ENABLE_LTO}")
message(STATUS "GEGL libs        : ${GEGL_LIBS}")

