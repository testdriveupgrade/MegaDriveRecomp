add_library(recomp config.cpp analyzer.cpp lifter.cpp)
target_link_libraries(recomp
    m68k_instruction
    m68k_target
    m68k_registers
    sega_rom_loader
    fmt::fmt-header-only
    spdlog::spdlog_header_only
)
target_compile_options(recomp PRIVATE -fexceptions)
