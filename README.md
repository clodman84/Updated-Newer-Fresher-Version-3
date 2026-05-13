# Building from Source

If you only need the application, download a prebuilt release from the Releases page.

## Requirements

Install the following before building:

| Tool   | Version | Notes                                |
| ------ | ------- | ------------------------------------ |
| Git    | latest  | Source checkout                      |
| CMake  | 3.20+   | Build configuration                  |
| Ninja  | latest  | Build backend                        |
| Python | 3.8+    | Required to install Ninja via `pipx` |
| vcpkg  | bundled | No separate install required         |

Platform-specific dependencies are listed below.

After installing Git, CMake, or Python, restart your terminal so they are available in `PATH`.

---

## Clone the Repository

```bash
git clone https://github.com/clodman84/Updated-Newer-Fresher-Version-3.git
cd Updated-Newer-Fresher-Version-3
```

---

## Install Ninja

```bash
pipx install ninja
```

If `pipx` is missing:

```bash
pip install pipx
pipx install ninja
```

---

## Install System Dependencies

### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
    pkg-config nasm \
    libx11-dev libxext-dev libxft-dev libxcursor-dev \
    libxi-dev libxinerama-dev libxrandr-dev libxss-dev \
    libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
    libvulkan-dev libxtst-dev
```

---

### macOS

Requires Homebrew.

```bash
brew update
brew install nasm pkg-config
```

---

### Windows

Install with Chocolatey (Administrator PowerShell):

```powershell
choco install pkgconfiglite -y
choco install nsis -y
```

You also need a GCC toolchain (MinGW). Supported options:

* MSYS2
* WinLibs

MSVC is not supported.

---

## Configure

### Linux

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUNFV3_ENABLE_LTO=ON \
    -DUNFV3_ENABLE_TRACY=OFF \
    "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux-mixed \
    -DVCPKG_OVERLAY_TRIPLETS=./triplets/
```

---

### macOS (Apple Silicon)

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUNFV3_ENABLE_LTO=ON \
    -DUNFV3_ENABLE_TRACY=OFF \
    "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=arm64-osx-dynamic
```

Intel macOS:

```bash
-DVCPKG_TARGET_TRIPLET=x64-osx-dynamic
```

---

### Windows (PowerShell)

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DUNFV3_ENABLE_LTO=ON `
    -DUNFV3_ENABLE_TRACY=OFF `
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
    -DCMAKE_C_COMPILER=gcc `
    -DCMAKE_CXX_COMPILER=g++
```

vcpkg will fetch and build required dependencies automatically during configuration.

---

## Build

```bash
cmake --build build --config Release
```

The output binary will be placed in the build directory.

---

## Package (Optional)

To generate a distributable package:

```bash
cd build
cpack -C Release
```

Generated artifacts (`.exe`, `.dmg`, `.tar.gz`) are written to `build/`.

---

## Troubleshooting

### `VCPKG_ROOT` is not set

Set it manually.

**bash/zsh**

```bash
export VCPKG_ROOT=/path/to/vcpkg
```

**PowerShell**

```powershell
$env:VCPKG_ROOT="C:\path\to\vcpkg"
```

---

### Missing dependency / library resolution failure

Delete the build directory and reconfigure:

```bash
rm -rf build
```

Then rerun the configure command.

---

### `gcc` / `g++` not found (Windows)

Ensure your MinGW `bin` directory is in `PATH`.

Typical MSYS2 path:

```text
C:\msys64\mingw64\bin
```

---

### Missing `lib*-dev` package (Linux)

Re-run the dependency install command and check for package manager errors.

---

## Build Options

| Flag                         | Default | Description                               |
| ---------------------------- | ------- | ----------------------------------------- |
| `-DUNFV3_ENABLE_LTO=ON`      | ON      | Enables link-time optimization            |
| `-DUNFV3_ENABLE_TRACY=OFF`   | OFF     | Enables Tracy profiling support           |
| `-DCMAKE_BUILD_TYPE=Release` | Release | Use `Debug` for symbols / no optimization |

---

## Reporting Build Issues

Open an issue and include:

* OS and version
* Compiler version
* Full configure/build output
* The exact failing step
