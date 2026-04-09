project('UNFV3', 'cpp', 'c',
  version : '0.1',
  meson_version : '>= 1.1',
  default_options : ['cpp_std=c++20', 'c_std=c11', 'buildtype=release', 'optimization=3', 'b_lto=true']
)

cmake = import('cmake')
cpp = meson.get_compiler('cpp')

if cpp.get_id() in ['gcc', 'clang']
  add_project_arguments('-fomit-frame-pointer', language : ['c', 'cpp'])
elif cpp.get_id() == 'msvc'
  # MSVC speed optimisations (function-level linking, data-section splitting)
  add_project_arguments('/Gy', '/Gw', language : ['c', 'cpp'])
endif

# --- Dependencies ---

# GEGL (Mandatory System Dependency)
gegl_dep = dependency('gegl-0.4', required : true)

# SDL3 (Vendored)
sdl_opts = cmake.subproject_options()
sdl_opts.add_cmake_defines({
  'SDL_SHARED': 'OFF',
  'SDL_STATIC': 'ON',
  'SDL_TEST': 'OFF',
})
sdl_subproj = cmake.subproject('SDL', options: sdl_opts, required: true)
sdl_dep = sdl_subproj.dependency('SDL3-static')

# OpenCV (Vendored)
opencv_opts = cmake.subproject_options()
opencv_opts.add_cmake_defines({
  'BUILD_LIST': 'core,imgproc,imgcodecs,dnn,objdetect',
  'BUILD_TESTS': 'OFF',
  'BUILD_PERF_TESTS': 'OFF',
  'BUILD_EXAMPLES': 'OFF',
  'BUILD_DOCS': 'OFF',
  'WITH_IPP': 'OFF',
  'WITH_OPENCL': 'OFF',
  'WITH_OPENGL': 'OFF',
  'WITH_CUDA': 'OFF',
  'WITH_KLEIDICV': 'OFF',
  'BUILD_opencv_apps': 'OFF',
})

# ImGui (Static Library from source)
imgui_inc = include_directories(
  'vendored/imgui',
  'vendored/imgui/backends',
  'vendored/imgui/misc/cpp'
)

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
  include_directories : [imgui_inc],
  dependencies : [sdl_dep]
)

imgui_dep = declare_dependency(
  link_with : imgui_lib,
  include_directories : imgui_inc
)

# SQLite (Static Library from source)
sqlite_inc = include_directories('vendored/sqlite-amalgamation-3510200')
sqlite_lib = static_library('sqlite',
  'vendored/sqlite-amalgamation-3510200/sqlite3.c',
  include_directories : sqlite_inc,
  c_args : [
    '-DSQLITE_ENABLE_FTS5',
    '-DSQLITE_ENABLE_JSON1',
    '-DSQLITE_ENABLE_RTREE',
    '-DSQLITE_THREADSAFE=2',
    '-DSQLITE_DEFAULT_MEMSTATUS=0',
    '-DSQLITE_LIKE_DOESNT_MATCH_BLOBS',
    '-DSQLITE_MAX_EXPR_DEPTH=0',
    '-DSQLITE_OMIT_DEPRECATED',
    '-DSQLITE_USE_ALLOCA'
  ]
)
sqlite_dep = declare_dependency(
  link_with : sqlite_lib,
  include_directories : sqlite_inc
)

# nlohmann_json
json_dep = declare_dependency(include_directories : include_directories('vendored/json'))

# Tracy (Optional)
tracy_dep = []
if get_option('tracy')
  tracy_subproj = subproject('tracy')
  tracy_dep = tracy_subproj.get_variable('tracy_dep')
  add_project_arguments('-DTRACY_ENABLE', language : 'cpp')
endif

# --- Main Application ---

src = files(
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

app_inc = include_directories('src/Application')

extra_libs = []
if host_machine.system() == 'windows'
  extra_libs += [
    cpp.find_library('ws2_32'),
    cpp.find_library('comctl32'),
    cpp.find_library('setupapi'),
    cpp.find_library('version')
  ]
endif

# OpenCV as a subproject
opencv_subproj = cmake.subproject('opencv', options: opencv_opts, required: true)
# Map OpenCV libraries manually as they might not be cleanly exported by the cmake module in all versions
opencv_dep = [
  opencv_subproj.dependency('opencv_core'),
  opencv_subproj.dependency('opencv_imgproc'),
  opencv_subproj.dependency('opencv_imgcodecs'),
  opencv_subproj.dependency('opencv_dnn'),
  opencv_subproj.dependency('opencv_objdetect')
]

executable('UNFV3',
  src,
  include_directories : app_inc,
  dependencies : [
    gegl_dep,
    sdl_dep,
    opencv_dep,
    imgui_dep,
    sqlite_dep,
    json_dep,
    tracy_dep
  ] + extra_libs,
  install : true
)
