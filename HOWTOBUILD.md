# HOWTOBUILD - UNFV3

This guide provides instructions for building UNFV3 from source using the Meson build system.

## Prerequisites

- **C++20 Compiler**: GCC 11+, Clang 13+, or MSVC 2022+.
- **Meson** (1.0+) and **Ninja**.
- **GEGL 0.4**: Development headers must be installed on your system.
  - Linux: `sudo apt install libgegl-dev`
  - macOS: `brew install gegl`
  - Windows: Ensure GEGL is in your `PKG_CONFIG_PATH`.
- **CMake**: Required by Meson to build the vendored SDL3 and OpenCV subprojects.

## Development Build

To set up a build directory for development (with debug symbols and no LTO):

```bash
# Initialize the build directory
meson setup build -Dbuildtype=debug

# Compile the project
ninja -C build

# Run the application
./build/UNFV3
```

### Enabling Tracy Profiler

To build with Tracy instrumentation:

```bash
meson setup build_tracy -Dtracy=true
ninja -C build_tracy
```

## Distribution Build

For a highly optimized release build:

```bash
# Initialize with release settings (O3 and LTO are enabled by default in our meson.build)
meson setup build_release --buildtype=release

# Compile
ninja -C build_release
```

## Cross-Platform Notes

### Windows (MSVC)

1. Open the "Developer Command Prompt for VS 2022".
2. Run the `meson setup` command. Meson will automatically detect the MSVC compiler.

### Linux

Ensure that `pkg-config` can find `gegl-0.4.pc`. If you installed GEGL in a non-standard location, set your `PKG_CONFIG_PATH` accordingly.

### macOS

The build system supports Apple Silicon. Ensure you have the latest Xcode Command Line Tools installed.

## Troubleshooting

- **Missing Subprojects**: The build system expects SDL3 and OpenCV in `vendored/`. If they are missing, ensure you have initialized git submodules:
  ```bash
  git submodule update --init --recursive
  ```
- **GEGL Not Found**: If `dependency('gegl-0.4')` fails, verify that `pkg-config --modversion gegl-0.4` works in your terminal.
