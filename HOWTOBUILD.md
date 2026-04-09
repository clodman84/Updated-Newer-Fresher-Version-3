# How to Build UNFV3

UNFV3 uses the **Meson** build system and **Ninja** as the backend. It requires a C++20 compliant compiler.

## Prerequisites

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential meson ninja-build libgegl-dev libbabl-dev libglib2.0-dev
```

### Windows
1. **MSYS2 (Recommended)**:
   - Install MSYS2 from [msys2.org](https://www.msys2.org/).
   - Open the "MSYS2 MINGW64" terminal.
   - Run:
     ```bash
     pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-meson mingw-w64-x86_64-ninja mingw-w64-x86_64-gegl mingw-w64-x86_64-babl
     ```
2. **Visual Studio / vcpkg**:
   - Ensure you have Visual Studio 2022 or newer with C++ desktop development workload.
   - Install `gegl` and `babl` via vcpkg:
     ```bash
     vcpkg install gegl babl
     ```

## Build Instructions

### 1. Development Build (Debug)
This build is suitable for debugging and rapid development.
```bash
meson setup builddir --buildtype=debug
meson compile -C builddir
```

### 2. Distribution Build (Optimized Release)
This build is highly optimized for performance, enabling LTO and speed optimizations.
```bash
meson setup build-release --buildtype=release -Db_lto=true
meson compile -C build-release
```

### 3. Optional Features
To enable the **Tracy Profiler**:
```bash
meson setup build-tracy -Dtracy=true
meson compile -C build-tracy
```

## Running the Application
After a successful build, the binary will be located in the output directory (`builddir/UNFV3` or `builddir\UNFV3.exe`).

### GEGL and Babl on Windows
If you are on Windows and the application fails to find GEGL operations, ensure that the GEGL and Babl environment variables are set correctly or that their DLLs and data files are in the same directory as the executable.

Typical environment variables:
- `GEGL_PATH`: Path to the directory containing GEGL operation DLLs (e.g., `C:\msys64\mingw64\lib\gegl-0.4`).
- `BABL_PATH`: Path to the directory containing Babl extension DLLs (e.g., `C:\msys64\mingw64\lib\babl-0.1`).
