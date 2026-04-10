function(bundle_runtime_dependencies TARGET_NAME BUNDLE_DIR)
    # Define platform-specific exclusions (Evaluated at configure time)
    if(WIN32)
        set(_pre "PRE_EXCLUDE_REGEXES" "\"api-ms-win-.*\"" "\"ext-ms-win-.*\"")
        set(_post "POST_EXCLUDE_REGEXES" "\"(?i)^([a-z]:)?/windows/.*\"") 
    elseif(APPLE)
        set(_pre "")
        set(_post "POST_EXCLUDE_REGEXES" "\"^/usr/lib/.*\"" "\"^/System/.*\"")
    else()
        set(_pre "")
        # Added GLib family, D-Bus, udev, and Audio drivers to the exclusion list
        set(_post 
            "POST_EXCLUDE_REGEXES"
            "\".*/lib(gcc_s|stdc.*|pthread|dl|rt|m|c|resolv)\\\\.so.*\""
            "\".*/ld-linux.*\\\\.so.*\""
            "\".*/lib(X11|Xext|xcb|GL|EGL|GLX|fontconfig|freetype|drm|wayland-.*)\\\\.so.*\""
            "\".*/lib(glib|gio|gobject|gmodule|gthread)-.*\\\\.so.*\""
            "\".*/lib(dbus-.*|asound|pulse.*|udev)\\\\.so.*\""
        )
    endif()

    # Convert CMake lists into a space-separated string to safely inject into the install script
    string(REPLACE ";" " " _pre_str "${_pre}")
    string(REPLACE ";" " " _post_str "${_post}")

    install(CODE "
        set(_bundle_dest \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib\")
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES $<TARGET_FILE:${TARGET_NAME}>
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
            ${_pre_str}
            ${_post_str}
        )
        foreach(_lib \${_resolved_deps})
            # Added quotes around \${_lib} to handle potential spaces in paths safely
            file(INSTALL \"\${_lib}\" DESTINATION \"\${_bundle_dest}\" FOLLOW_SYMLINK_CHAIN)
        endforeach()
    " COMPONENT Runtime)

    # Get the directories where the system installed GEGL and Babl plugins
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GEGL REQUIRED gegl-0.4)
    pkg_check_modules(BABL REQUIRED babl-0.1)

    pkg_get_variable(GEGL_PLUGINS_DIR gegl-0.4 pluginsdir)
    pkg_get_variable(BABL_PLUGINS_DIR babl-0.1 pluginsdir)

    # Brute-force copy ALL plugins for both GEGL and Babl into the bundle
    install(CODE "
        # Copy GEGL plugins
        file(INSTALL \"${GEGL_PLUGINS_DIR}/\" 
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/gegl-0.4\" 
             FOLLOW_SYMLINK_CHAIN)

        # Copy BABL plugins
        file(INSTALL \"${BABL_PLUGINS_DIR}/\" 
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib/babl-0.1\" 
             FOLLOW_SYMLINK_CHAIN)
    " COMPONENT Runtime)

endfunction()
