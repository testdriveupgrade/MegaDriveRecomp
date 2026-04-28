#pragma once
#include "lib/m68k/registers/registers.h"
#include "lib/sega/memory/m68k_ram_device.h"
#include "lib/sega/memory/vdp_device.h"
#include "lib/sega/memory/z80_device.h"
#include <string_view>

namespace sega {

class StateDump {
public:
  StateDump(VdpDevice& vdp_device, M68kRamDevice& m68k_ram_device, Z80RamDevice& z80_ram_device,
            m68k::Registers& registers);

  // VDP-only dump (for tooling/inspection)
  static void save_dump_to_file(const VdpDevice& vdp_device, std::string_view path);
  static void apply_dump_from_file(VdpDevice& vdp_device, std::string_view path);

  // Full save state to/from a `.st` file: registers, M68K RAM, Z80 RAM, full VDP state.
  void save_state_to_file(std::string_view path) const;
  void load_state_from_file(std::string_view path);

private:
  VdpDevice& vdp_device_;
  M68kRamDevice& m68k_ram_device_;
  Z80RamDevice& z80_ram_device_;
  m68k::Registers& registers_;
};

} // namespace sega
