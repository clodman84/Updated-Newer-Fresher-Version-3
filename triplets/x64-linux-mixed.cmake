set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Everything static by default
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CRT_LINKAGE dynamic)

# But glib, gegl, babl must be dynamic to share a single GObject registry
if(PORT MATCHES "^(glib|gobject-introspection|libffi|pcre2|gettext)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
