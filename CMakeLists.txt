cmake_minimum_required(VERSION 3.20)
project(UNFV3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --------------------------------------------------
# Global build type default
# --------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# LTO for release (supported on all three platforms)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# --------------------------------------------------
# Platform: Windows static-runtime + no DLLs
# --------------------------------------------------

if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX VC_EXTRALEAN)
endif()

# --------------------------------------------------
# Cross-platform compiler flags
#
# Intentionally NO -march=native here: that flag encodes the *build*
# machine's ISA into the binary, which silently breaks on any other CPU.
# OpenCV's own CPU dispatcher handles SIMD at runtime instead (see below).
# --------------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -fomit-frame-pointer
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
        $<$<CONFIG:RelWithDebInfo>:-O2>
    )
elseif(MSVC)
    add_compile_options(
        /fp:fast
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

# --------------------------------------------------
# SDL3
# --------------------------------------------------

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL)

# --------------------------------------------------
# OpenCV  --  find system install first, build only if missing
#
# WHY: building OpenCV from source takes 5-20 min.  A system/prebuilt
# install (brew, apt, vcpkg, the official Windows installer) is instant.
#
# HOW TO INSTALL (one-time, per machine):
#   macOS:   brew install opencv
#   Ubuntu:  sudo apt install libopencv-dev
#   Windows: winget install opencv  OR  use vcpkg:
#            vcpkg install opencv[dnn]:x64-windows-static
#            then pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#
# If none of the above are present the submodule is built as a last resort,
# with every unnecessary module and dependency stripped out.
# --------------------------------------------------

# We only need these modules — tell find_package to check for them.
# objdetect is required for cv::FaceDetectorYN (lives there, not in dnn).
set(UNFV3_OPENCV_COMPONENTS core imgproc imgcodecs dnn objdetect)

find_package(OpenCV QUIET COMPONENTS ${UNFV3_OPENCV_COMPONENTS})

if(OpenCV_FOUND)
    message(STATUS "Found system OpenCV ${OpenCV_VERSION} -- skipping source build")
    # find_package sets OpenCV_LIBS and OpenCV_INCLUDE_DIRS automatically.
    set(OPENCV_LINK_LIBS ${OpenCV_LIBS})
else()
    message(STATUS "System OpenCV not found -- building from submodule (this will be slow)")
    message(STATUS "  Tip: install OpenCV system-wide to skip this (see CMakeLists comments)")

    # Everything must be static; set before any add_subdirectory
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # ---- Disable IPP (causes header-ordering build errors on Linux) ----
    set(WITH_IPP     OFF CACHE BOOL "" FORCE)
    set(WITH_IPP_IW  OFF CACHE BOOL "" FORCE)
    set(BUILD_IPP_IW OFF CACHE BOOL "" FORCE)

    # ---- Absolute minimum module set ----
    set(BUILD_LIST "core,imgproc,imgcodecs,dnn,objdetect" CACHE STRING "" FORCE)

    # ---- Strip every optional module / test / app ----
    foreach(_off_var
        BUILD_TESTS BUILD_PERF_TESTS BUILD_EXAMPLES BUILD_DOCS
        BUILD_opencv_apps BUILD_opencv_python2 BUILD_opencv_python3
        BUILD_opencv_java BUILD_opencv_gapi
        INSTALL_PYTHON_EXAMPLES INSTALL_C_EXAMPLES
        OPENCV_ENABLE_NONFREE
    )
        set(${_off_var} OFF CACHE BOOL "" FORCE)
    endforeach()

    # ---- Strip every optional backend to keep deps minimal ----
    foreach(_off_backend
        WITH_OPENCL WITH_OPENGL WITH_VTK WITH_EIGEN
        WITH_GSTREAMER WITH_FFMPEG WITH_1394
        WITH_V4L WITH_DC1394 WITH_ARAVIS
        WITH_CUDA WITH_CUDNN
        WITH_GTK WITH_QT
        WITH_WEBP WITH_OPENJPEG WITH_JASPER WITH_OPENEXR WITH_TIFF
    )
        set(${_off_backend} OFF CACHE BOOL "" FORCE)
    endforeach()

    # Keep minimal image format support for loading model blobs / test images
    set(WITH_JPEG ON CACHE BOOL "" FORCE)
    set(WITH_PNG  ON CACHE BOOL "" FORCE)

    # ---- DNN: vendored protobuf, no external deps ----
    set(BUILD_PROTOBUF       ON  CACHE BOOL "" FORCE)
    set(PROTOBUF_UPDATE_FILES OFF CACHE BOOL "" FORCE)

    # ---- SIMD: let OpenCV's runtime dispatcher handle it ----
    # SSE4_2 baseline is universally safe on x86-64 (any CPU from ~2008+).
    # AVX/AVX2 are dispatched at runtime so the binary runs everywhere.
    set(CPU_BASELINE "SSE4_2"   CACHE STRING "" FORCE)
    set(CPU_DISPATCH "AVX;AVX2" CACHE STRING "" FORCE)

    # ---- Parallel build hint for the sub-project ----
    # (CMake 3.12+) Doesn't speed up the outer build but helps ninja/make
    # inside the opencv subdirectory use all cores.
    set(CMAKE_BUILD_PARALLEL_LEVEL "" CACHE STRING "" FORCE)

    add_subdirectory(vendored/opencv)

    set(OPENCV_LINK_LIBS
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_dnn
        opencv_objdetect
    )
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

# SQLite is pure C and benefits from max opt regardless of build type
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sqlite PRIVATE -O3)
elseif(MSVC)
    target_compile_options(sqlite PRIVATE /O2)
endif()

# --------------------------------------------------
# nlohmann_json (header-only)
# --------------------------------------------------

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE vendored/json)

# --------------------------------------------------
# stb_image wrapper
# --------------------------------------------------

add_library(stb_image_wrapper STATIC
    src/Application/image.cpp
)

target_include_directories(stb_image_wrapper PRIVATE src/Application)

target_link_libraries(stb_image_wrapper PRIVATE
    imgui
    SDL3::SDL3
    sqlite
    nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stb_image_wrapper PRIVATE -O3 -ffast-math -funroll-loops)
elseif(MSVC)
    target_compile_options(stb_image_wrapper PRIVATE /O2)
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
)

target_include_directories(UNFV3 PRIVATE src/Application)

# Propagate OpenCV include dirs when using a system install
# (add_subdirectory sets them on targets directly; find_package needs this)
if(OpenCV_FOUND)
    target_include_directories(UNFV3 PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

target_link_libraries(UNFV3 PRIVATE
    imgui
    SDL3::SDL3
    sqlite
    nlohmann_json
    stb_image_wrapper
    ${OPENCV_LINK_LIBS}
)

# Windows system libs required by OpenCV / SDL at link time
if(WIN32)
    target_link_libraries(UNFV3 PRIVATE ws2_32 comctl32 setupapi version)
endif()

# --------------------------------------------------
# Tracy
# --------------------------------------------------

if(UNFV3_ENABLE_TRACY)
    target_link_libraries(UNFV3 PRIVATE TracyClient)
    target_compile_definitions(UNFV3 PRIVATE TRACY_ENABLE)
    target_link_libraries(stb_image_wrapper PRIVATE TracyClient)
    target_compile_definitions(stb_image_wrapper PRIVATE TRACY_ENABLE)
endif()

# --------------------------------------------------
# Diagnostics
# --------------------------------------------------

message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "System OpenCV    : ${OpenCV_FOUND}")
message(STATUS "OpenCV version   : ${OpenCV_VERSION}")
message(STATUS "OpenCV libs      : ${OPENCV_LINK_LIBS}")
