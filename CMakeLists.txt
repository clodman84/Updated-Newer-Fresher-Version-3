cmake_minimum_required(VERSION 3.21)
project(UNFV3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

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
option(UNFV3_ENABLE_TRACY "Enable Tracy profiler" OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(ipo_supported AND UNFV3_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "LTO enabled")
endif()

if(UNFV3_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<CONFIG:Release>:-flto=thin>)
    add_link_options($<$<CONFIG:Release>:-flto=thin>)
endif()

# --------------------------------------------------
# Project optimization profile (applies only to our targets)
# --------------------------------------------------

set(_gnu_clang "$<CXX_COMPILER_ID:GNU,Clang>")
set(_c_or_cxx  "$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>")

add_library(unfv3_project_optimizations INTERFACE)

target_compile_options(unfv3_project_optimizations INTERFACE
    $<$<AND:${_c_or_cxx},${_gnu_clang}>:-fomit-frame-pointer>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-O3>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-ffunction-sections>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-fdata-sections>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:RelWithDebInfo>>:-O2>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:MinSizeRel>>:-Os>
)

target_link_options(unfv3_project_optimizations INTERFACE
    $<$<AND:${_c_or_cxx},$<PLATFORM_ID:Linux>,${_gnu_clang},$<CONFIG:Release>>:-Wl,--gc-sections>
    $<$<AND:${_c_or_cxx},$<PLATFORM_ID:Darwin>,${_gnu_clang},$<CONFIG:Release>>:-Wl,-dead_strip>
)

# --------------------------------------------------
# Dependencies
# --------------------------------------------------

find_package(SDL3 REQUIRED)
find_package(ncnn REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(CURL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GOBJECT REQUIRED IMPORTED_TARGET gobject-2.0)
pkg_check_modules(GEGL REQUIRED IMPORTED_TARGET gegl-0.4)
pkg_check_modules(BABL REQUIRED IMPORTED_TARGET babl-0.1)

if(UNFV3_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

# --------------------------------------------------
# Application library + main executable
# --------------------------------------------------

add_subdirectory(src/Application)

add_executable(UNFV3 MACOSX_BUNDLE src/main.cpp)

target_link_libraries(UNFV3 PRIVATE unfv3_application)

if(WIN32)
    target_link_libraries(UNFV3 PRIVATE ws2_32 comctl32 setupapi version)
    target_link_options(UNFV3 PRIVATE -static-libgcc -static-libstdc++)
endif()

# --------------------------------------------------
# Post-build
# --------------------------------------------------

set(VCPKG_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")

add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${VCPKG_LIB_DIR}/babl-0.1"
    "$<TARGET_FILE_DIR:UNFV3>/lib/babl-0.1"
    COMMENT "Copying vcpkg BABL plugins to build directory"
)

add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${VCPKG_LIB_DIR}/gegl-0.4"
    "$<TARGET_FILE_DIR:UNFV3>/lib/gegl-0.4"
    COMMENT "Copying vcpkg GEGL plugins to build directory"
)

add_custom_command(TARGET UNFV3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Data"
    "$<TARGET_FILE_DIR:UNFV3>/Data"
    COMMENT "Copying Data folder to build directory"
)

# --------------------------------------------------
# Install rules (consumed by CPack)
# --------------------------------------------------

include(GNUInstallDirs)

set(UNFV3_INSTALL_BINDIR ".")

if(APPLE)
    set(UNFV3_INSTALL_LIBDIR "UNFV3.app/Contents/MacOS/lib")
else()
    set(UNFV3_INSTALL_LIBDIR "lib")
endif()

install(TARGETS UNFV3
    BUNDLE  DESTINATION "."
    RUNTIME DESTINATION "${UNFV3_INSTALL_BINDIR}"
)

install(DIRECTORY "${VCPKG_LIB_DIR}/babl-0.1/"
    DESTINATION "${UNFV3_INSTALL_LIBDIR}/babl-0.1"
)

install(DIRECTORY "${VCPKG_LIB_DIR}/gegl-0.4/"
    DESTINATION "${UNFV3_INSTALL_LIBDIR}/gegl-0.4"
)

if(APPLE)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Data/" DESTINATION "UNFV3.app/Contents/MacOS")
else()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Data/" DESTINATION "./Data")
endif()

if(NOT APPLE AND NOT WIN32)
    install(DIRECTORY "${VCPKG_LIB_DIR}/"
        DESTINATION "${UNFV3_INSTALL_LIBDIR}"
        FILES_MATCHING
        PATTERN "*.so*"
        PATTERN "babl-0.1" EXCLUDE
        PATTERN "gegl-0.4" EXCLUDE
        PATTERN "pkgconfig" EXCLUDE
    )
endif()

if(WIN32)
    install(CODE [[
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "$<TARGET_FILE:UNFV3>"
            RESOLVED_DEPENDENCIES_VAR   _resolved
            UNRESOLVED_DEPENDENCIES_VAR _unresolved
            PRE_EXCLUDE_REGEXES
                "^api-ms-"
                "^ext-ms-"
                "^hvsifiletrust"
            POST_EXCLUDE_REGEXES
                ".*[Ss]ystem32/.*\\.dll$"
                ".*[Ww]in[Ss][Xx][Ss]/.*"
        )
        foreach(_dll ${_resolved})
            file(INSTALL "${_dll}" DESTINATION "${CMAKE_INSTALL_PREFIX}/.")
        endforeach()
        if(_unresolved)
            message(WARNING "Unresolved runtime deps: ${_unresolved}")
        endif()
    ]])
endif()

if(APPLE)
    install(CODE "
        include(BundleUtilities)
        set(APPS \"\${CMAKE_INSTALL_PREFIX}/UNFV3.app\")
        set(DIRS \"${VCPKG_LIB_DIR}\")
        fixup_bundle(\"\${APPS}\" \"\" \"\${DIRS}\")
    ")
    install(CODE "
        message(STATUS \"Repairing ad-hoc signature...\")
        execute_process(COMMAND codesign --force --deep --sign - \"\${CMAKE_INSTALL_PREFIX}/UNFV3.app\")
    ")
endif()

if(NOT APPLE AND NOT WIN32)
    install(CODE "
        message(STATUS \"Stripping bundled shared libraries and plugins...\")
        file(GLOB_RECURSE _bundled_sos \"\${CMAKE_INSTALL_PREFIX}/${UNFV3_INSTALL_LIBDIR}/*.so*\")
        foreach(_so \${_bundled_sos})
            execute_process(COMMAND strip --strip-unneeded \"\${_so}\" ERROR_QUIET)
        endforeach()
    ")
endif()

if(WIN32)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/"
        DESTINATION "."
        FILES_MATCHING PATTERN "*.dll")
endif()

# --------------------------------------------------
# CPack
# --------------------------------------------------

set(CPACK_PACKAGE_NAME                "UNFV3")
set(CPACK_PACKAGE_VENDOR              "DoPySOFT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The DoPy Billing Software")
set(CPACK_PACKAGE_VERSION_MAJOR       "0")
set(CPACK_PACKAGE_VERSION_MINOR       "7")
set(CPACK_PACKAGE_VERSION_PATCH       "0")
set(CPACK_PACKAGE_VERSION
    "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "UNFV3")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES                 TRUE)
set(CPACK_RESOURCE_FILE_LICENSE       "${CMAKE_CURRENT_SOURCE_DIR}/License.txt")
set(CPACK_PACKAGE_HOMEPAGE_URL        "https://github.com/clodman84/Updated-Newer-Fresher-Version-3")

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME                     "UNFV3")
    set(CPACK_NSIS_PACKAGE_NAME                     "UNFV3")
    set(CPACK_NSIS_INSTALL_ROOT                     "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL  ON)
    set(CPACK_NSIS_MODIFY_PATH                      OFF)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\UNFV3.lnk' '$INSTDIR\\\\UNFV3.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\UNFV3.lnk'")
elseif(APPLE)
    set(CPACK_GENERATOR       "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "UNFV3")
    set(CPACK_DMG_FORMAT      "UDZO")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "LTO enabled      : ${UNFV3_ENABLE_LTO}")
message(STATUS "Tracy enabled    : ${UNFV3_ENABLE_TRACY}")
message(STATUS "Compiler         : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CPack generators : ${CPACK_GENERATOR}")
