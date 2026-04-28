#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio_output.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace sega {

// Defined in the .cpp so the free callback function can access it.
struct AudioImpl {
  ma_device device;
  ma_pcm_rb ring_buffer;
  bool initialised{false};
};

static void data_callback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frame_count) {
  auto* impl = static_cast<AudioImpl*>(dev->pUserData);

  ma_uint32 frames_remaining = frame_count;
  auto* out = static_cast<int16_t*>(output);

  while (frames_remaining > 0) {
    void* read_ptr = nullptr;
    ma_uint32 frames_available = frames_remaining;
    if (ma_pcm_rb_acquire_read(&impl->ring_buffer, &frames_available, &read_ptr) != MA_SUCCESS) {
      break;
    }
    if (frames_available == 0) {
      break;
    }
    std::memcpy(out, read_ptr, frames_available * AudioOutput::kChannels * sizeof(int16_t));
    ma_pcm_rb_commit_read(&impl->ring_buffer, frames_available);
    out += frames_available * AudioOutput::kChannels;
    frames_remaining -= frames_available;
  }

  if (frames_remaining > 0) {
    std::memset(out, 0, frames_remaining * AudioOutput::kChannels * sizeof(int16_t));
  }
}

static AudioImpl* as_impl(void* p) { return static_cast<AudioImpl*>(p); }

AudioOutput::AudioOutput() : impl_{new AudioImpl{}} {}

AudioOutput::~AudioOutput() {
  auto* impl = as_impl(impl_);
  if (impl->initialised) {
    ma_device_stop(&impl->device);
    ma_device_uninit(&impl->device);
    ma_pcm_rb_uninit(&impl->ring_buffer);
  }
  delete impl;
}

bool AudioOutput::init() {
  auto* impl = as_impl(impl_);

  if (ma_pcm_rb_init(ma_format_s16, kChannels, 4096, nullptr, nullptr, &impl->ring_buffer) != MA_SUCCESS) {
    spdlog::error("AudioOutput: ring buffer init failed");
    return false;
  }

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format   = ma_format_s16;
  cfg.playback.channels = kChannels;
  cfg.sampleRate        = kSampleRate;
  cfg.dataCallback      = data_callback;
  cfg.pUserData         = impl;

  if (ma_device_init(nullptr, &cfg, &impl->device) != MA_SUCCESS) {
    spdlog::error("AudioOutput: device init failed");
    ma_pcm_rb_uninit(&impl->ring_buffer);
    return false;
  }

  if (ma_device_start(&impl->device) != MA_SUCCESS) {
    spdlog::error("AudioOutput: device start failed");
    ma_device_uninit(&impl->device);
    ma_pcm_rb_uninit(&impl->ring_buffer);
    return false;
  }

  impl->initialised = true;
  spdlog::info("AudioOutput: started at {} Hz stereo", kSampleRate);
  return true;
}

size_t AudioOutput::available_write_frames() const {
  auto* impl = as_impl(impl_);
  if (!impl->initialised) {
    return 0;
  }
  return ma_pcm_rb_available_write(&impl->ring_buffer);
}

void AudioOutput::push(const int16_t* samples, size_t frame_count) {
  auto* impl = as_impl(impl_);
  if (!impl->initialised) {
    return;
  }
  ma_uint32 remaining = static_cast<ma_uint32>(frame_count);
  const int16_t* src = samples;

  while (remaining > 0) {
    void* write_ptr = nullptr;
    ma_uint32 frames_to_write = remaining;
    if (ma_pcm_rb_acquire_write(&impl->ring_buffer, &frames_to_write, &write_ptr) != MA_SUCCESS) {
      break;
    }
    if (frames_to_write == 0) {
      break;
    }
    std::memcpy(write_ptr, src, frames_to_write * kChannels * sizeof(int16_t));
    ma_pcm_rb_commit_write(&impl->ring_buffer, frames_to_write);
    src += frames_to_write * kChannels;
    remaining -= frames_to_write;
  }
}

} // namespace sega
