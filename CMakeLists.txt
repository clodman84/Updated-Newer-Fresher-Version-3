cmake_minimum_required(VERSION 3.20)
project(UNFV3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL3
add_subdirectory(vendored/SDL)

# ImGui
add_library(imgui STATIC
    vendored/imgui/imgui.cpp
    vendored/imgui/imgui_draw.cpp
    vendored/imgui/imgui_tables.cpp
    vendored/imgui/imgui_widgets.cpp
    vendored/imgui/imgui_demo.cpp
    vendored/imgui/backends/imgui_impl_sdl3.cpp
    vendored/imgui/backends/imgui_impl_sdlgpu3.cpp
)

target_include_directories(imgui PUBLIC
    vendored/imgui
    vendored/imgui/backends
    vendored/SDL/include
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_executable(UNFV3
    src/main.cpp
    src/Application/image.cpp
)

target_include_directories(UNFV3 PRIVATE
    src/Application
    vendored/SDL/include
)

target_link_libraries(UNFV3 PRIVATE
    imgui
    SDL3::SDL3
)
