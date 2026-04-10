function(bundle_runtime_dependencies TARGET_NAME BUNDLE_DIR)
    # 1. Discover shared library dependencies
    
    # Define platform-specific exclusions
    set(_pre_exclude "")
    set(_post_exclude "")

    if(WIN32)
        # Windows: Exclude low-level API sets and core OS directories
        set(_pre_exclude "api-ms-win-.*" "ext-ms-win-.*")
        set(_post_exclude "(?i)^[A-Z]:/Windows/.*")
    elseif(APPLE)
        # macOS: Exclude Apple's core system libraries and frameworks.
        # (Homebrew/MacPorts libs live in /opt/homebrew, /usr/local, or /opt/local, so they get bundled)
        set(_post_exclude "^/usr/lib/.*" "^/System/.*")
    else()
        # Linux: We CANNOT exclude /usr/lib broadly because system GEGL lives there.
        # Instead, exclude specific foundational OS and desktop libraries.
        set(_post_exclude 
            ".*/lib(gcc_s|stdc\\+\\+|pthread|dl|rt|m|c|resolv)\\.so.*"
            ".*/lib(X11|Xext|xcb|GL|EGL|GLX|fontconfig|freetype|drm)\\.so.*"
        )
    endif()

    install(CODE "
        set(_bundle_dest \"\${CMAKE_INSTALL_PREFIX}/${BUNDLE_DIR}/lib\")
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \$<TARGET_FILE:${TARGET_NAME}>
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
            PRE_EXCLUDE_REGEXES \${_pre_exclude}
            POST_EXCLUDE_REGEXES \${_post_exclude}
        )
        foreach(_lib \${\_resolved_deps})
            file(INSTALL \${\_lib} DESTINATION \${\_bundle_dest} FOLLOW_SYMLINK_CHAIN)
        endforeach()
    " COMPONENT Runtime)

    # 2. Bundle MINIMAL GEGL and Babl modules
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GEGL REQUIRED gegl-0.4)
    pkg_check_modules(BABL REQUIRED babl-0.1)

    pkg_get_variable(GEGL_PLUGINS_DIR gegl-0.4 pluginsdir)
    pkg_get_variable(BABL_PLUGINS_DIR babl-0.1 pluginsdir)

    # Use python script to detect actually needed GEGL ops
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/detect_gegl_ops.py ${CMAKE_SOURCE_DIR}/src ${GEGL_PLUGINS_DIR}
        OUTPUT_VARIABLE GEGL_MATCHED_MODULES
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    string(REPLACE "\n" ";" GEGL_MODULE_LIST "${GEGL_MATCHED_MODULES}")

    foreach(_mod ${GEGL_MODULE_LIST})
        install(FILES ${_mod} DESTINATION ${BUNDLE_DIR}/lib/gegl-0.4 COMPONENT Runtime)
    endforeach()

    # Install BABL modules
    install(DIRECTORY ${BABL_PLUGINS_DIR}/ DESTINATION ${BUNDLE_DIR}/lib/babl-0.1 COMPONENT Runtime)

endfunction()
