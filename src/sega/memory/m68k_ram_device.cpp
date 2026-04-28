#include "m68k_ram_device.h"
#include "lib/common/error/error.h"
#include "lib/common/memory/types.h"
#include "lib/common/util/passkey.h"
#include <algorithm>
#include <cstddef>
#include <optional>

namespace sega {

M68kRamDevice::M68kRamDevice() {
  data_.resize(kEnd - kBegin + 1);
}

void M68kRamDevice::clear() {
  std::fill(data_.begin(), data_.end(), 0);
}

DataView M68kRamDevice::dump_state(Passkey<StateDump>) const {
  return DataView{data_.data(), data_.size()};
}

void M68kRamDevice::apply_state(Passkey<StateDump>, DataView state) {
  const auto count = std::min(state.size(), data_.size());
  std::copy_n(state.data(), count, data_.begin());
}

std::optional<Error> M68kRamDevice::read(AddressType addr, MutableDataView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = data_[addr - kBegin + i];
  }
  return std::nullopt;
}

std::optional<Error> M68kRamDevice::write(AddressType addr, DataView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    data_[addr - kBegin + i] = data[i];
  }
  return std::nullopt;
}

} // namespace sega
