add_library(sega_state_dump state_dump.cpp)
target_link_libraries(sega_state_dump sega_memory m68k_registers spdlog::spdlog_header_only)
