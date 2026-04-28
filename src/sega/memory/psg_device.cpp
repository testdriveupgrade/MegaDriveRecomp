#include "psg_device.h"
#include "lib/common/error/error.h"
#include "lib/common/memory/types.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <spdlog/spdlog.h>

namespace sega {

namespace {

// 2 dB attenuation per step; index 15 = silence
static constexpr int16_t kVolTable[16] = {
    32767, 26028, 20675, 16422, 13045, 10362, 8231, 6538,
    5193,  4125,  3277,  2603,  2067,  1642,  1304, 0
};

// PSG input clock / 16 gives the tone counter clock.
// PSG input on Mega Drive = 3,579,545 Hz.
// Ticks per 44100 Hz output sample:
static constexpr double kPsgTicksPerSample = 3'579'545.0 / 16.0 / 44100.0; // ≈ 5.073

} // namespace

std::optional<Error> PsgDevice::write(AddressType /*addr*/, DataView data) {
  const uint8_t byte = data.as<Byte>();
  spdlog::debug("PSG write: {:02x}", byte);

  std::lock_guard lock(mutex_);
  if (byte & 0x80) {
    // LATCH byte: sets channel and type
    latch_ch_  = (byte >> 5) & 0x3;
    latch_vol_ = (byte >> 4) & 0x1;
    if (latch_vol_) {
      channels_[latch_ch_].volume = byte & 0xF;
    } else {
      // low 4 bits of period
      channels_[latch_ch_].period = (channels_[latch_ch_].period & 0x3F0) | (byte & 0xF);
      if (channels_[latch_ch_].period == 0) {
        channels_[latch_ch_].period = 1;
      }
    }
  } else {
    // DATA byte: high 6 bits of period (only valid for tone channels 0-2)
    if (!latch_vol_ && latch_ch_ < 3) {
      channels_[latch_ch_].period = ((byte & 0x3F) << 4) | (channels_[latch_ch_].period & 0xF);
      if (channels_[latch_ch_].period == 0) {
        channels_[latch_ch_].period = 1;
      }
    } else if (latch_vol_) {
      channels_[latch_ch_].volume = byte & 0xF;
    }
  }
  return std::nullopt;
}

void PsgDevice::generate(int16_t* out, size_t frame_count) {
  std::lock_guard lock(mutex_);
  for (size_t i = 0; i < frame_count; ++i) {
    psg_acc_ += kPsgTicksPerSample;
    const int ticks = static_cast<int>(psg_acc_);
    psg_acc_ -= ticks;

    for (int t = 0; t < ticks; ++t) {
      // tone channels 0-2
      for (int ch = 0; ch < 3; ++ch) {
        if (--channels_[ch].counter <= 0) {
          channels_[ch].counter = channels_[ch].period;
          channels_[ch].output  = !channels_[ch].output;
        }
      }
      // noise channel 3
      if (--channels_[3].counter <= 0) {
        // SN76489 noise rate: low 2 bits of period select shift period
        // 0→16 ticks, 1→32, 2→64, 3→use channel 2's period.
        // (Earlier this used 2/4/8 — that ran the LFSR ~8× too fast and
        // produced high-frequency buzz that aliased above Nyquist.)
        const uint16_t rate = (channels_[3].period & 0x3) == 3
                                  ? channels_[2].period
                                  : (16u << (channels_[3].period & 0x3));
        channels_[3].counter = static_cast<int>(rate);

        // 16-bit Galois LFSR; white noise taps at bits 0 and 3
        const bool feedback = (channels_[3].period & 0x4)
                                  ? ((lfsr_ & 1) ^ ((lfsr_ >> 3) & 1))
                                  : (lfsr_ & 1);
        lfsr_ = static_cast<uint16_t>((lfsr_ >> 1) | (feedback ? 0x8000u : 0u));
        channels_[3].output = (lfsr_ & 1);
      }
    }

    // Mix all four channels. Each PSG channel is a square wave around zero,
    // so it contributes +vol when its flip-flop is high and -vol when low —
    // emitting +vol/0 (as before) cuts the amplitude in half and adds a DC
    // offset that audio drivers can't really filter out cleanly. Divide by 4
    // to leave headroom for all four channels at full volume.
    int32_t sample = 0;
    for (int ch = 0; ch < 4; ++ch) {
      if (channels_[ch].volume < 15) {
        const int16_t v = kVolTable[channels_[ch].volume];
        sample += channels_[ch].output ? v : -v;
      }
    }
    sample /= 4;
    sample = std::clamp(sample, -32767, 32767);
    out[i * 2]     = static_cast<int16_t>(sample);
    out[i * 2 + 1] = static_cast<int16_t>(sample);
  }
}

} // namespace sega
