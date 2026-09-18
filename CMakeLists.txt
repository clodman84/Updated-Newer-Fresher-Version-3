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
find_package(OpenSSL REQUIRED)

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

add_executable(UNFV3 src/main.cpp)

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
set(UNFV3_INSTALL_LIBDIR "lib")

install(TARGETS UNFV3
    RUNTIME DESTINATION "${UNFV3_INSTALL_BINDIR}"
)

install(DIRECTORY "${VCPKG_LIB_DIR}/babl-0.1/"
    DESTINATION "${UNFV3_INSTALL_LIBDIR}/babl-0.1"
)

install(DIRECTORY "${VCPKG_LIB_DIR}/gegl-0.4/"
    DESTINATION "${UNFV3_INSTALL_LIBDIR}/gegl-0.4"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Data/"
    DESTINATION "./Data"
)

if(APPLE)
    install(DIRECTORY "${VCPKG_LIB_DIR}/"
        DESTINATION "${UNFV3_INSTALL_LIBDIR}"
        FILES_MATCHING
        PATTERN "*.dylib*"
        PATTERN "babl-0.1" EXCLUDE
        PATTERN "gegl-0.4" EXCLUDE
        PATTERN "pkgconfig" EXCLUDE
    )
elseif(NOT WIN32)
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

    foreach(_mingw_dll
        libgcc_s_seh-1.dll
        libgomp-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )
        find_file(_mingw_dll_path_${_mingw_dll} NAMES ${_mingw_dll})
        if(_mingw_dll_path_${_mingw_dll})
            install(FILES "${_mingw_dll_path_${_mingw_dll}}" DESTINATION ".")
        else()
            message(WARNING "Could not locate ${_mingw_dll} to bundle")
        endif()
    endforeach()
endif()

if (APPLE)
	install(CODE [[
	    file(GLOB _plugin_dylibs
		"${CMAKE_INSTALL_PREFIX}/lib/babl-0.1/*.dylib"
		"${CMAKE_INSTALL_PREFIX}/lib/gegl-0.4/*.dylib"
	    )
	    foreach(_dylib ${_plugin_dylibs})
		execute_process(COMMAND otool -L "${_dylib}" OUTPUT_VARIABLE _deps)
		string(REGEX MATCHALL "/Users/[^ \n]+\\.dylib" _abs_paths "${_deps}")
		foreach(_abs ${_abs_paths})
		    get_filename_component(_libname "${_abs}" NAME)
		    execute_process(COMMAND install_name_tool -change "${_abs}" "@rpath/${_libname}" "${_dylib}")
		endforeach()
	    endforeach()
	]])
endif()

if(NOT WIN32)
    if(APPLE)
        set(_strip_flag "-x")
    else()
        set(_strip_flag "--strip-unneeded")
    endif()
    install(CODE "
        message(STATUS \"Stripping bundled shared libraries and plugins...\")
        file(GLOB_RECURSE _bundled_libs \"\${CMAKE_INSTALL_PREFIX}/${UNFV3_INSTALL_LIBDIR}/*${CMAKE_SHARED_LIBRARY_SUFFIX}*\")
        foreach(_lib \${_bundled_libs})
            execute_process(COMMAND strip ${_strip_flag} \"\${_lib}\" ERROR_QUIET)
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
set(CPACK_PACKAGE_VERSION_MINOR       "5")
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
    set(CPACK_NSIS_DEFINES "Unicode true")
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
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "LTO enabled      : ${UNFV3_ENABLE_LTO}")
message(STATUS "Tracy enabled    : ${UNFV3_ENABLE_TRACY}")
message(STATUS "Compiler         : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CPack generators : ${CPACK_GENERATOR}")
