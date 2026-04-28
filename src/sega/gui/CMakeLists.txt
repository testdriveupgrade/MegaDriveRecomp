add_library(sega_gui gui.cpp)
target_link_libraries(
    sega_gui
    sega_audio
    sega_image_saver
    sega_video
    sega_shader
    spdlog::spdlog_header_only
    imgui
)
