function(bundle_runtime_dependencies TARGET_NAME BUNDLE_DIR)
    # Define platform-specific exclusions
    if(WIN32)
        set(_pre "PRE_EXCLUDE_REGEXES" "\"api-ms-win-.*\"" "\"ext-ms-win-.*\"")
        set(_post "POST_EXCLUDE_REGEXES" "\"(?i)^([a-z]:)?/windows/.*\"") 
    elseif(APPLE)
        set(_pre "")
        set(_post "POST_EXCLUDE_REGEXES" "\"^/usr/lib/.*\"" "\"^/System/.*\"")
    else()
        set(_pre "")
        set(_post 
            "POST_EXCLUDE_REGEXES"
            "\".*/lib(gcc_s|stdc.*|pthread|dl|rt|m|c|resolv)\\\\.so.*\""
            "\".*/ld-linux.*\\\\.so.*\""
            "\".*/lib(X11|Xext|xcb|GL|EGL|GLX|fontconfig|freetype|drm|wayland-.*)\\\\.so.*\""
            "\".*/lib(glib|gio|gobject|gmodule|gthread)-.*\\\\.so.*\""
            "\".*/lib(dbus-.*|asound|pulse.*|udev)\\\\.so.*\""
        )
    endif()

    string(REPLACE ";" " " _pre_str "${_pre}")
    string(REPLACE ";" " " _post_str "${_post}")

    # NEW: Extract the absolute Windows path to the MSYS2/MinGW bin directory
    # This guarantees we find libgcc_s, libstdc++, libwinpthread, and libbabl
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GEGL REQUIRED gegl-0.4)
    pkg_check_modules(BABL REQUIRED babl-0.1)

    pkg_get_variable(GEGL_PLUGINS_DIR gegl-0.4 pluginsdir)
    pkg_get_variable(BABL_PLUGINS_DIR babl-0.1 pluginsdir)

    install(CODE "
        # Windows needs core DLLs in the root folder next to the .exe
        if(WIN32)
            set(_bundle_dest \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}\")
        else()
            set(_bundle_dest \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib\")
        endif()

        # 1. Copy plugins into the lib folder (Always stays in lib/ for GEGL_PATH logic)
        file(INSTALL \"${GEGL_PLUGINS_DIR}/\" 
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/gegl-0.4\" 
             FOLLOW_SYMLINK_CHAIN)

        file(INSTALL \"${BABL_PLUGINS_DIR}/\" 
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/babl-0.1\" 
             FOLLOW_SYMLINK_CHAIN)

        # 2. Glob plugins
        file(GLOB_RECURSE _plugin_files 
            \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/gegl-0.4/*\${CMAKE_SHARED_LIBRARY_SUFFIX}\"
            \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/babl-0.1/*\${CMAKE_SHARED_LIBRARY_SUFFIX}\"
        )

        # 3. Scan dependencies and force CMake to look in the TRUE MinGW bin directory
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES $<TARGET_FILE:${TARGET_NAME}>
            LIBRARIES \${_plugin_files}
            DIRECTORIES \"${MINGW_BIN_DIR}\"
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
            ${_pre_str}
            ${_post_str}
        )

        # Print a loud warning if anything was missed
        if(_unresolved_deps)
            message(WARNING \"\\n--- MISSING DEPENDENCIES ---\\nCMake could not find:\\n\${_unresolved_deps}\\n---------------------------\\n\")
        endif()

        # 4. Copy core DLLs to the correct OS destination
        foreach(_lib \${_resolved_deps})
            file(INSTALL \"\${_lib}\" DESTINATION \"\${_bundle_dest}\" FOLLOW_SYMLINK_CHAIN)
        endforeach()
    " COMPONENT Runtime)
endfunction()
