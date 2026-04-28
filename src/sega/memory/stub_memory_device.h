#pragma once
#include "lib/common/error/error.h"
#include "lib/common/memory/device.h"
#include "lib/common/memory/types.h"
#include <optional>
#include <vector>

namespace sega {

// Zero-initialized RAM-like device that succeeds for any read/write inside
// [begin, end].  Used for cartridge-specific extra regions (SRAM, lock-on
// areas, etc.) that the live emulator doesn't model in detail.
class StubMemoryDevice : public Device {
public:
  StubMemoryDevice(AddressType begin, AddressType end);

private:
  std::optional<Error> read(AddressType addr, MutableDataView data) override;
  std::optional<Error> write(AddressType addr, DataView data) override;

  AddressType begin_;
  std::vector<Byte> data_;
};

} // namespace sega
