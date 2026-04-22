add_library(unfv3_application STATIC
    database.cpp
    database_search_engine.cpp
    detection.cpp
    export_manager.cpp
    gpu_utils.cpp
    image.cpp
    image_editor.cpp
    image_editor_ui.cpp
    image_manager.cpp
    session_billed_ui.cpp
    session_carousel_ui.cpp
    session_image_ui.cpp
    session_keyboard_nav.cpp
    session_search_ui.cpp
    operations/gimp_levels.cpp
    operations/my_colour_enhance.cpp
)

target_include_directories(unfv3_application
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${GEGL_INCLUDE_DIRS}
)

target_link_libraries(unfv3_application PUBLIC
    unfv3_project_optimizations
    imgui::imgui
    SDL3::SDL3
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    ncnn
    PkgConfig::GLIB
    PkgConfig::GOBJECT
    PkgConfig::GEGL
    PkgConfig::BABL
)

target_link_libraries(unfv3_application PRIVATE $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>)

if(UNFV3_ENABLE_TRACY)
    target_link_libraries(unfv3_application PUBLIC Tracy::TracyClient)
    target_compile_definitions(unfv3_application PUBLIC TRACY_ENABLE)
endif()
