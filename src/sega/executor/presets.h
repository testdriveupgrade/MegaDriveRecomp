#pragma once
#include "lib/common/memory/types.h"
#include <string_view>
#include <vector>

namespace sega {

// Per-cartridge tweak applied at boot: extra writable memory regions to map
// onto the bus.  Selected by matching the ROM's serial-number field.
struct Preset {
  struct Range {
    AddressType begin;
    AddressType end;
  };

  std::string_view name;
  std::string_view serial; // matched against Metadata::serial_number (trailing spaces ignored)
  std::vector<Range> extra_memory;
};

// Returns the matching preset, or nullptr if none is registered for this serial.
const Preset* find_preset(std::string_view serial);

} // namespace sega
