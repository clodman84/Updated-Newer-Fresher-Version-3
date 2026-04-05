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
# OpenCV  --  static-first configuration
#
# Default is vendored static OpenCV so the app does not depend on OpenCV
# shared libraries at runtime.
# --------------------------------------------------

set(UNFV3_OPENCV_COMPONENTS core imgproc imgcodecs dnn objdetect)
if(UNFV3_USE_SYSTEM_OPENCV)
    find_package(OpenCV QUIET COMPONENTS ${UNFV3_OPENCV_COMPONENTS})
endif()

if(OpenCV_FOUND AND UNFV3_USE_SYSTEM_OPENCV)
    message(STATUS "Found system OpenCV ${OpenCV_VERSION} -- using system package")
    set(OPENCV_LINK_LIBS ${OpenCV_LIBS})
else()
    message(STATUS "Using vendored static OpenCV")

    # Only the five modules we actually use.
    # Must be set before add_subdirectory so OpenCV's CMake sees it.
    set(BUILD_LIST "core,imgproc,imgcodecs,dnn,objdetect" CACHE STRING "" FORCE)

    # No tests, docs, examples, language bindings.
    set(BUILD_TESTS             OFF CACHE BOOL "" FORCE)
    set(BUILD_PERF_TESTS        OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES          OFF CACHE BOOL "" FORCE)
    set(BUILD_DOCS              OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_apps       OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python2    OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python3    OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_java       OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_gapi       OFF CACHE BOOL "" FORCE)
    set(OPENCV_ENABLE_NONFREE   OFF CACHE BOOL "" FORCE)
    set(INSTALL_PYTHON_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(INSTALL_C_EXAMPLES      OFF CACHE BOOL "" FORCE)

    # No optional I/O or compute backends.
    set(WITH_IPP        OFF CACHE BOOL "" FORCE)
    set(WITH_IPP_IW     OFF CACHE BOOL "" FORCE)
    set(BUILD_IPP_IW    OFF CACHE BOOL "" FORCE)
    set(WITH_OPENCL     OFF CACHE BOOL "" FORCE)
    set(WITH_OPENGL     OFF CACHE BOOL "" FORCE)
    set(WITH_VTK        OFF CACHE BOOL "" FORCE)
    set(WITH_EIGEN      OFF CACHE BOOL "" FORCE)
    set(WITH_GSTREAMER  OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG     OFF CACHE BOOL "" FORCE)
    set(WITH_1394       OFF CACHE BOOL "" FORCE)
    set(WITH_V4L        OFF CACHE BOOL "" FORCE)
    set(WITH_DC1394     OFF CACHE BOOL "" FORCE)
    set(WITH_ARAVIS     OFF CACHE BOOL "" FORCE)
    set(WITH_CUDA       OFF CACHE BOOL "" FORCE)
    set(WITH_CUDNN      OFF CACHE BOOL "" FORCE)
    set(WITH_GTK        OFF CACHE BOOL "" FORCE)
    set(WITH_QT         OFF CACHE BOOL "" FORCE)
    set(WITH_WEBP       OFF CACHE BOOL "" FORCE)
    set(WITH_OPENJPEG   OFF CACHE BOOL "" FORCE)
    set(WITH_JASPER     OFF CACHE BOOL "" FORCE)
    set(WITH_OPENEXR    OFF CACHE BOOL "" FORCE)
    set(WITH_TIFF       OFF CACHE BOOL "" FORCE)

    # Keep JPEG and PNG for loading model blobs / test images.
    set(WITH_JPEG ON CACHE BOOL "" FORCE)
    set(WITH_PNG  ON CACHE BOOL "" FORCE)

    # Vendored protobuf for dnn; no external dep.
    set(BUILD_PROTOBUF       ON  CACHE BOOL "" FORCE)
    set(PROTOBUF_UPDATE_FILES OFF CACHE BOOL "" FORCE)

    # FIX (macOS arm64): OpenCV's Kleidi NEON microkernel (libkleidicv)
    # produces unresolved symbols at link time on Apple Silicon.  Disabling
    # it makes OpenCV fall back to its plain C++ NEON path, which is correct.
    set(WITH_KLEIDICV OFF CACHE BOOL "" FORCE)

    # x86-64 SIMD only — don't set these on ARM (macOS M-series, aarch64).
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(CPU_BASELINE "SSE4_2"   CACHE STRING "" FORCE)
        set(CPU_DISPATCH "AVX;AVX2" CACHE STRING "" FORCE)
    endif()

    add_subdirectory(vendored/opencv)

    set(OPENCV_LINK_LIBS
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_dnn
        opencv_objdetect
    )

    file(GLOB _opencv_module_includes 
    LIST_DIRECTORIES true 
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/opencv/modules/*/include"
	)

    set(OPENCV_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/vendored/opencv/include"
    "${CMAKE_CURRENT_BINARY_DIR}"
    ${_opencv_module_includes}
)
endif()

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
)

target_include_directories(UNFV3 PRIVATE src/Application)
target_include_directories(UNFV3 PRIVATE ${OPENCV_INCLUDE_DIRS})
target_include_directories(UNFV3 PRIVATE ${GEGL_INCLUDE_DIRS})

target_link_libraries(UNFV3 PRIVATE
    imgui
    SDL3::SDL3
    sqlite
    nlohmann_json
    unfv3_project_optimizations
    ${OPENCV_LINK_LIBS}
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
message(STATUS "System OpenCV    : ${OpenCV_FOUND}")
message(STATUS "OpenCV version   : ${OpenCV_VERSION}")
message(STATUS "OpenCV libs      : ${OPENCV_LINK_LIBS}")
message(STATUS "OpenCV_INCLUDE_DIRS after add_subdirectory: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "GEGL libs        : ${GEGL_LIBS}")
