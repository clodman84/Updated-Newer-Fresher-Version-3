project('UNFV3', 'cpp', 'c',
  version : '3.0.0',
  default_options : [
    'cpp_std=c++20',
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    'b_lto=true',
  ]
)

cpp = meson.get_compiler('cpp')

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# Mandatory dependencies
gegl_dep = dependency('gegl-0.4', required : get_option('ignore_missing_deps') == false)
babl_dep = dependency('babl-0.1', required : get_option('ignore_missing_deps') == false)
glib_dep = dependency('glib-2.0', required : get_option('ignore_missing_deps') == false)
gobject_dep = dependency('gobject-2.0', required : get_option('ignore_missing_deps') == false)

# CMake module for vendored dependencies
cmake = import('cmake')

# SDL3 via CMake
sdl3_opts = cmake.subproject_options()
sdl3_opts.add_cmake_defines({
  'SDL_SHARED': false,
  'SDL_STATIC': true,
  'SDL_TEST': false,
  'SDL_X11_XCURSOR': false,
  'SDL_X11_XFIXES': false,
  'SDL_X11_XINPUT': false,
  'SDL_X11_XRANDR': false,
  'SDL_X11_XSHAPE': false,
  'SDL_X11_XTEST': false,
  'SDL_X11_XDBE': false,
  'SDL_X11_XINERAMA': false,
  'SDL_X11_XSYNC': false,
  'SDL_X11_XSCRNSAVER': false,
  'SDL_VIDEO_DRIVER_X11': false,
  'SDL_VIDEO_DRIVER_WAYLAND': false,
})
sdl3_proj = cmake.subproject('SDL', options: sdl3_opts)
sdl3_dep = sdl3_proj.dependency('SDL3-static')
sdl3_inc = include_directories('vendored/SDL/include')

# OpenCV via CMake
opencv_opts = cmake.subproject_options()
opencv_opts.add_cmake_defines({
  'BUILD_LIST': 'core,imgproc,imgcodecs,dnn,objdetect',
  'BUILD_EXAMPLES': false,
  'BUILD_TESTS': false,
  'BUILD_PERF_TESTS': false,
  'BUILD_DOCS': false,
  'BUILD_opencv_apps': false,
  'WITH_IPP': false,
  'WITH_OPENCL': false,
  'WITH_OPENGL': false,
  'WITH_CUDA': false,
  'WITH_GTK': false,
  'WITH_QT': false,
  'WITH_WEBP': false,
  'WITH_OPENJPEG': false,
  'WITH_JASPER': false,
  'WITH_OPENEXR': false,
  'WITH_TIFF': false,
  'WITH_JPEG': true,
  'WITH_PNG': true,
})
if get_option('ignore_missing_deps') == false
  opencv_proj = cmake.subproject('opencv', options: opencv_opts)
  opencv_dep = [
    opencv_proj.dependency('opencv_core'),
    opencv_proj.dependency('opencv_imgproc'),
    opencv_proj.dependency('opencv_imgcodecs'),
    opencv_proj.dependency('opencv_dnn'),
    opencv_proj.dependency('opencv_objdetect')
  ]
else
  opencv_dep = dependency('', required : false)
endif

# -----------------------------------------------------------------------------
# Internal Libraries (Vendored)
# -----------------------------------------------------------------------------

# ImGui
imgui_inc = [include_directories('vendored/imgui', 'vendored/imgui/backends'), sdl3_inc]
imgui_src = files(
  'vendored/imgui/imgui.cpp',
  'vendored/imgui/imgui_draw.cpp',
  'vendored/imgui/imgui_tables.cpp',
  'vendored/imgui/imgui_widgets.cpp',
  'vendored/imgui/imgui_demo.cpp',
  'vendored/imgui/backends/imgui_impl_sdl3.cpp',
  'vendored/imgui/backends/imgui_impl_sdlgpu3.cpp',
  'vendored/imgui/misc/cpp/imgui_stdlib.cpp'
)
imgui_lib = static_library('imgui',
  imgui_src,
  include_directories : imgui_inc,
  dependencies : sdl3_dep
)
imgui_dep = declare_dependency(
  link_with : imgui_lib,
  include_directories : imgui_inc,
  dependencies : sdl3_dep
)

# SQLite
sqlite_inc = include_directories('vendored/sqlite-amalgamation-3510200')
sqlite_lib = static_library('sqlite',
  'vendored/sqlite-amalgamation-3510200/sqlite3.c',
  include_directories : sqlite_inc,
  c_args : [
    '-DSQLITE_ENABLE_FTS5',
    '-DSQLITE_ENABLE_JSON1',
    '-DSQLITE_ENABLE_RTREE',
    '-DSQLITE_THREADSAFE=2'
  ]
)
sqlite_dep = declare_dependency(
  link_with : sqlite_lib,
  include_directories : sqlite_inc
)

# JSON
json_dep = declare_dependency(include_directories : include_directories('vendored/json'))

# Tracy (Optional)
tracy_dep = dependency('', required : false)
if get_option('tracy')
  tracy_proj = subproject('tracy')
  tracy_dep = tracy_proj.get_variable('tracy_dep')
  add_project_arguments('-DTRACY_ENABLE', language : 'cpp')
endif

# -----------------------------------------------------------------------------
# Main Executable
# -----------------------------------------------------------------------------

app_inc = [include_directories('src/Application'), sdl3_inc]

app_src = files(
  'src/main.cpp',
  'src/Application/database.cpp',
  'src/Application/session.cpp',
  'src/Application/search_engine.cpp',
  'src/Application/detection.cpp',
  'src/Application/ui.cpp',
  'src/Application/image_editor.cpp',
  'src/Application/image.cpp',
  'src/Application/gpu_utils.cpp',
  'src/Application/operations/gimp_levels.cpp',
  'src/Application/operations/my_colour_enhance.cpp'
)

# Platform specific arguments
extra_args = []
extra_link_args = []
if host_machine.system() == 'windows'
  extra_link_args += ['-lws2_32', '-lcomctl32', '-lsetupapi', '-lversion']
  add_project_arguments('-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DVC_EXTRALEAN', language : ['c', 'cpp'])
endif

# Release optimizations
if get_option('buildtype') == 'release'
  if cpp.get_id() == 'msvc'
    add_project_arguments('/O2', '/Gy', '/Gw', language : 'cpp')
  else
    add_project_arguments('-O3', '-fomit-frame-pointer', language : 'cpp')
  endif
endif

executable('UNFV3',
  app_src,
  include_directories : app_inc,
  dependencies : [
    sdl3_dep,
    opencv_dep,
    gegl_dep,
    babl_dep,
    glib_dep,
    gobject_dep,
    imgui_dep,
    sqlite_dep,
    json_dep,
    tracy_dep
  ],
  cpp_args : extra_args,
  link_args : extra_link_args,
  install : true,
  override_options : ['cpp_std=c++20']
)
