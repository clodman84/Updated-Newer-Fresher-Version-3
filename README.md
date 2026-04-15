# Build Instructions

You need to have vcpkg installed. This program does not compile on MSVC due to it's GEGL dependency. Use MinGW.

## Release Build

1. Setup

Add -DUNV3_ENABLE_TRACY=ON if you have tracy and want to do speedtests and so on

```
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNFV3_ENABLE_LTO=ON -DUNFV3_AGGRESSIVE_STRIP=OFF -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux-mixed -DVCPKG_OVERLAY_TRIPLETS=./triplets/ -DCMAKE_INSTALL_PREFIX=./release/bundle
```

2. Build

```
cmake --build release
```

3. Bundle 

```
cmake --install release
```

3. Package

```
cpack --config release/CPackConfig.cmake
```
