#include "stub_memory_device.h"
#include "lib/common/error/error.h"
#include "lib/common/memory/types.h"
#include <cstddef>
#include <optional>

namespace sega {

StubMemoryDevice::StubMemoryDevice(AddressType begin, AddressType end)
    : begin_{begin}, data_(end - begin + 1, Byte{0}) {}

std::optional<Error> StubMemoryDevice::read(AddressType addr, MutableDataView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = data_[addr - begin_ + i];
  }
  return std::nullopt;
}

std::optional<Error> StubMemoryDevice::write(AddressType addr, DataView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    data_[addr - begin_ + i] = data[i];
  }
  return std::nullopt;
}

} // namespace sega
