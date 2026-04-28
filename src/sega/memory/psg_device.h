#pragma once
#include "lib/common/error/error.h"
#include "lib/common/memory/device.h"
#include "lib/common/memory/types.h"
#include <cstdint>
#include <mutex>
#include <optional>

namespace sega {

class PsgDevice : public WriteOnlyDevice {
public:
  static constexpr AddressType kBegin = 0xC00011;
  static constexpr AddressType kEnd = 0xC00012;

  // Generate `frame_count` stereo int16 samples into interleaved `out`.
  // Safe to call concurrently with write() — internally synchronized.
  void generate(int16_t* out, size_t frame_count);

private:
  std::optional<Error> write(AddressType addr, DataView data) override;

  struct Channel {
    uint16_t period{1};   // tone: 10-bit; noise: 4-bit shift rate
    uint8_t  volume{15};  // 4-bit attenuation (15 = mute)
    uint16_t counter{0};
    bool     output{false};
  };

  // Protects all the chip state below. Audio thread holds it across generate();
  // emulation thread takes it briefly per register write.
  std::mutex mutex_;
  Channel channels_[4]{};
  int     latch_ch_{0};
  bool    latch_vol_{false};
  uint16_t lfsr_{0x8000};   // noise LFSR
  double  psg_acc_{0.0};    // fractional PSG-tick accumulator
};

} // namespace sega
