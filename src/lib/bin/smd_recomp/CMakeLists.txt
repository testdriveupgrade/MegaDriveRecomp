add_executable(smd_recomp main.cpp)
target_link_libraries(smd_recomp recomp spdlog::spdlog_header_only)
target_compile_options(smd_recomp PRIVATE -fexceptions)
