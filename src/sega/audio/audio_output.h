#pragma once
#include <cstddef>
#include <cstdint>

namespace sega {

// Wraps miniaudio for stereo 44100 Hz PCM output.
// Call push() from the game loop; miniaudio drains asynchronously.
class AudioOutput {
public:
  static constexpr int kSampleRate = 44100;
  static constexpr int kChannels = 2;

  AudioOutput();
  ~AudioOutput();

  // Returns false if initialisation failed (audio will be silent).
  bool init();

  // Push interleaved stereo int16 samples. Safe to call from any thread.
  void push(const int16_t* samples, size_t frame_count);

  // Number of frames the ring buffer can currently accept without dropping.
  // Use this to throttle the producer instead of blocking inside push().
  size_t available_write_frames() const;

private:
  void* impl_{};
};

} // namespace sega
