#include "ym2612_device.h"
#include "lib/common/error/error.h"
#include "lib/common/memory/types.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <spdlog/spdlog.h>

namespace sega {

namespace {

// YM2612 master clock on NTSC Mega Drive = 7,670,454 Hz.
// One OPN2_Clock() call = 1 internal clock = 6 master clocks.
// Internal clocks per 44100 Hz output sample:
//   7,670,454 / 6 / 44100 ≈ 28.99
// (One full DAC pass through all 6 channels takes 24 internal clocks, giving
// the chip's native ~53 kHz output rate; we accumulate the per-cycle MOL/MOR
// across ~29 internal clocks to mix into one output sample.)
static constexpr double kOpn2InternalClocksPerSample = 7'670'454.0 / 6.0 / 44100.0;

} // namespace

Ym2612Device::Ym2612Device() {
  // Select YM2612 DAC behavior (the Mega Drive variant). Default is YM3438
  // readmode, which has a different per-cycle output pattern.
  OPN2_SetChipType(ym3438_mode_ym2612 | ym3438_mode_readmode);
  OPN2_Reset(&chip_);
}

std::optional<Error> Ym2612Device::read(AddressType addr, MutableDataView data) {
  spdlog::debug("YM2612 read address: {:06x} size: {}", addr, data.size());
  std::lock_guard lock(mutex_);
  for (auto& v : data) {
    v = OPN2_Read(&chip_, static_cast<uint32_t>(addr - kBegin));
  }
  return std::nullopt;
}

std::optional<Error> Ym2612Device::write(AddressType addr, DataView data) {
  const uint8_t byte = data.as<Byte>();
  const uint32_t port = static_cast<uint32_t>(addr - kBegin);
  spdlog::debug("YM2612 write port: {} byte: {:02x}", port, byte);
  std::lock_guard lock(mutex_);
  OPN2_Write(&chip_, port, byte);
  return std::nullopt;
}

void Ym2612Device::generate(int16_t* out, size_t frame_count) {
  std::lock_guard lock(mutex_);
  for (size_t i = 0; i < frame_count; ++i) {
    acc_ += kOpn2InternalClocksPerSample;
    // Sum every cycle's MOL/MOR pin state across the ~29 internal clocks that
    // belong to this output sample. The DAC outputs one channel per cycle
    // (out_en cycles) plus a small sign pad on the others, so the sum is the
    // multiplexed 6-channel mix. Just keeping the last value (as we did
    // before) lost 5 of 6 channels and produced harsh aliasing.
    int32_t sum_l = 0, sum_r = 0;
    int count = 0;
    while (acc_ >= 1.0) {
      Bit16s buf[2];
      OPN2_Clock(&chip_, buf);
      sum_l += buf[0];
      sum_r += buf[1];
      ++count;
      acc_ -= 1.0;
    }
    if (count > 0) {
      last_[0] = sum_l;
      last_[1] = sum_r;
    }
    // The accumulated sum already represents all 6 channels mixed (vs the
    // previous "last cycle only" code which captured 1 of 6), so the gain
    // here is much smaller than before. *2 leaves enough headroom for the
    // executor to mix this with PSG without slamming the int16 ceiling —
    // when both ran at full peak the clipping sounded like an explosion.
    const int32_t l = std::clamp(last_[0] * 2, -32768, 32767);
    const int32_t r = std::clamp(last_[1] * 2, -32768, 32767);
    out[i * 2]     = static_cast<int16_t>(l);
    out[i * 2 + 1] = static_cast<int16_t>(r);
  }
}

} // namespace sega
