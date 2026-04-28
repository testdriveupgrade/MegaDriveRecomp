#pragma once
#include "lib/common/error/error.h"
#include "lib/common/memory/device.h"
#include "lib/common/memory/types.h"
#include <cstdint>
#include <mutex>
#include <optional>

extern "C" {
#include "ym3438.h"
}

namespace sega {

class Ym2612Device : public Device {
public:
  static constexpr AddressType kBegin = 0xA04000;
  static constexpr AddressType kEnd   = 0xA04003;

  Ym2612Device();

  // Generate `frame_count` stereo int16 samples into interleaved `out`.
  // Safe to call concurrently with read()/write() — internally synchronized.
  void generate(int16_t* out, size_t frame_count);

private:
  std::optional<Error> read(AddressType addr, MutableDataView data) override;
  std::optional<Error> write(AddressType addr, DataView data) override;

  // Protects chip_, acc_, last_. The audio thread holds this for the duration
  // of generate(); the emulation thread takes it briefly per register write.
  std::mutex mutex_;
  ym3438_t chip_{};
  double   acc_{0.0};   // fractional OPN2 clock accumulator
  int32_t  last_[2]{};  // last generated L/R samples
};

} // namespace sega
