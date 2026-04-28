add_library(
    sega_memory
    bus_device.cpp
    controller_device.cpp
    m68k_ram_device.cpp
    psg_device.cpp
    rom_device.cpp
    sram_access_register_device.cpp
    stub_memory_device.cpp
    trademark_register_device.cpp
    vdp_device.cpp
    ym2612_device.cpp
    z80_device.cpp
    # Nuked-OPN2 C source (compiled as C, linked into the library)
    ${NUKEDOPN2_SOURCE_DIR}/ym3438.c
)
target_link_libraries(sega_memory memory spdlog::spdlog_header_only fmt::fmt-header-only)
