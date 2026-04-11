function(bundle_runtime_dependencies TARGET_NAME BUNDLE_DIR)
    # Define platform-specific exclusions
    if(WIN32)
        set(_pre "PRE_EXCLUDE_REGEXES" "\"api-ms-win-.*\"" "\"ext-ms-win-.*\"")
        
        # The force-block list: Stop the garbage DLLs from entering the bundle
        set(_post "POST_EXCLUDE_REGEXES" 
            "\"(?i)^([a-z]:)?/windows/.*\""
            "\"(?i).*libgfortran.*\\\\.dll\""
            "\"(?i).*libquadmath.*\\\\.dll\""
            "\"(?i).*libopenblas.*\\\\.dll\""
            "\"(?i).*libicudata.*\\\\.dll\""
            "\"(?i).*avcodec.*\\\\.dll\""
            "\"(?i).*avformat.*\\\\.dll\""
            "\"(?i).*avutil.*\\\\.dll\""
            "\"(?i).*libraw.*\\\\.dll\""
            "\"(?i).*swscale.*\\\\.dll\""
        ) 
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

    # Extract the absolute path to the compiler's bin directory
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GEGL REQUIRED gegl-0.4)
    pkg_check_modules(BABL REQUIRED babl-0.1)

    pkg_get_variable(GEGL_PLUGINS_DIR gegl-0.4 pluginsdir)
    pkg_get_variable(BABL_PLUGINS_DIR babl-0.1 pluginsdir)

    install(CODE "
        if(WIN32)
            set(_bundle_dest \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}\")
        else()
            set(_bundle_dest \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib\")
        endif()

        # 1. Keep every plugin starting with 'gegl-'
        file(INSTALL \"${GEGL_PLUGINS_DIR}/\" 
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/gegl-0.4\" 
             FOLLOW_SYMLINK_CHAIN
             FILES_MATCHING
             PATTERN \"gegl-*\${CMAKE_SHARED_LIBRARY_SUFFIX}\"
        )

        file(INSTALL \"${BABL_PLUGINS_DIR}/\" 
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/babl-0.1\" 
             FOLLOW_SYMLINK_CHAIN)

        # 2. Glob ALL the copied plugins
        file(GLOB_RECURSE _plugin_files 
            \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/gegl-0.4/*\${CMAKE_SHARED_LIBRARY_SUFFIX}\"
            \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/babl-0.1/*\${CMAKE_SHARED_LIBRARY_SUFFIX}\"
        )

        # 3. Scan the executable AND all plugins
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES $<TARGET_FILE:${TARGET_NAME}>
            LIBRARIES \${_plugin_files}
            DIRECTORIES \"${MINGW_BIN_DIR}\"
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
            ${_pre_str}
            ${_post_str}
        )

        # It is normal to see warnings here about missing libgfortran, etc. 
        # This confirms the garbage is successfully blocked.
        if(_unresolved_deps)
            message(WARNING \"\\n--- BLOCKED DEPENDENCIES ---\\nCMake successfully excluded:\\n\${_unresolved_deps}\\n---------------------------\\n\")
        endif()

        # 4. Copy the safe, resolved dependencies into the bundle
        foreach(_lib \${_resolved_deps})
            file(INSTALL \"\${_lib}\" DESTINATION \"\${_bundle_dest}\" FOLLOW_SYMLINK_CHAIN)
        endforeach()
    " COMPONENT Runtime)
endfunction()
