#pragma once
#include "lib/common/error/error.h"
#include "lib/common/memory/device.h"
#include "lib/common/memory/types.h"
#include "lib/common/util/passkey.h"
#include <optional>
#include <random>
#include <vector>

namespace sega {

class Z80RamDevice : public Device {
public:
  static constexpr AddressType kBegin = 0xA00000;
  static constexpr AddressType kEnd = 0xA0FFFF;

  Z80RamDevice();

  // dump or apply whole Z80 RAM contents (used for save state)
  DataView dump_state(Passkey<class StateDump>) const;
  void apply_state(Passkey<StateDump>, DataView state);

private:
  using RandomBytesEngine = std::independent_bits_engine<std::default_random_engine, 8, Byte>;

private:
  std::optional<Error> read(AddressType addr, MutableDataView data) override;
  std::optional<Error> write(AddressType addr, DataView data) override;

private:
  RandomBytesEngine random_engine_;
  std::vector<Byte> ram_data_;
};

class Z80ControllerDevice : public Device {
public:
  static constexpr AddressType kBegin = 0xA11100;
  static constexpr AddressType kEnd = 0xA11201;

private:
  std::optional<Error> read(AddressType addr, MutableDataView data) override;
  std::optional<Error> write(AddressType addr, DataView data) override;

private:
  Word bus_value_{};
};

} // namespace sega
