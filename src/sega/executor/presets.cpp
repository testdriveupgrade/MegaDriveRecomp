#include "presets.h"
#include <string_view>
#include <vector>

namespace sega {

namespace {

const std::vector<Preset>& all_presets() {
  static const std::vector<Preset> presets = {
      {
          .name = "Sonic the Hedgehog",
          .serial = "GM 00001009-00",
          .extra_memory = {},
      },
      {
          .name = "Battle City (KRIKzz homebrew)",
          .serial = "GM   BT-103-00",
          .extra_memory = {},
      },
      {
          .name = "Test Drive II - The Duel",
          .serial = "GM ACLD008 -00",
          // The ROM probes $200000 (SRAM / lock-on area).  Map it as stub RAM
          // so reads return 0 and writes are absorbed instead of erroring.
          .extra_memory = {{0x200000, 0x3FFFFF}},
      },
  };
  return presets;
}

std::string_view trim_trailing_spaces(std::string_view s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
    s.remove_suffix(1);
  }
  return s;
}

} // namespace

const Preset* find_preset(std::string_view serial) {
  const auto trimmed = trim_trailing_spaces(serial);
  for (const auto& p : all_presets()) {
    if (trim_trailing_spaces(p.serial) == trimmed) {
      return &p;
    }
  }
  return nullptr;
}

} // namespace sega
