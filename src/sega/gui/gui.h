#pragma once
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "lib/sega/audio/audio_output.h"
#include "lib/sega/executor/executor.h"
#include "lib/sega/image_saver/image_saver.h"
#include "lib/sega/shader/shader.h"
#include "lib/sega/video/plane.h"
#include "lib/sega/video/sprite_table.h"
#include "lib/sega/video/tilemap.h"
#include "lib/sega/video/video.h"
#include <GL/gl.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace sega {

class Gui {
public:
  Gui(Executor& executor);
  ~Gui();

  bool setup();
  void loop();

private:
  // Poll events, returns false if should stop
  bool poll_events();

  // Set info about pressed buttons (main thread)
  void update_controller();

  // Render whole screen (main thread)
  void render();

  // Emulation thread body
  void emulation_thread_body();

  // Audio thread body — generates PSG+YM2612 samples independently of
  // the emulation thread so audio mixing cost can't slow down M68K execution.
  void audio_thread_body();

  // Thread-safe condition setters
  void set_condition(std::function<bool()> cond, bool reset_time = false);
  void clear_condition();
  bool is_running() const; // snapshot whether condition_ is non-null

  // Main window
  void add_main_window();

  // Game window
  void add_game_window();

  // Execution window
  void add_execution_window();
  void add_execution_window_statistics();
  void add_execution_window_instruction_info();
  void add_execution_window_commands();
  void add_execution_window_registers();

  // Colors window
  void add_colors_window();

  // Tilemap window
  void add_tilemap_window();

  // Plane window
  void add_plane_window(PlaneType plane_type);

  // Sprite table window
  void add_sprite_table_window();

  // Export all resources to PNG files
  void export_resources();

private:
  Executor& executor_;
  GLFWwindow* window_{};

  // Shader variables
  Shader shader_;
  GLuint shader_program_;
  ShaderType current_shader_type_{ShaderType::Nothing};

  // Game window
  bool show_game_window_{true};
  enum class GameSpeed {
    x0p25,
    x0p50,
    x1p00,
    x1p50,
    x2p00,
  } game_speed_{GameSpeed::x1p00};
  int game_scale_{1};
  Video video_;

  // Emulation thread
  std::thread emu_thread_;
  std::thread audio_thread_;
  std::atomic<bool> emu_running_{false};
  // VBlank sync: emulation thread signals after each VBlank, waits for render to complete
  std::mutex vblank_mutex_;
  std::condition_variable vblank_cv_;
  std::condition_variable render_cv_;
  bool vblank_ready_{false};
  bool render_done_{false};

  // Execution window
  bool show_execution_window_{true};
  std::array<char, 7> until_address_{};
  // Protects condition_ (written by main thread, read by emulation thread)
  mutable std::mutex condition_mutex_;
  std::function<bool()> condition_;
  uint64_t condition_generation_{0}; // incremented under condition_mutex_ each time condition_ is set
  std::atomic<uint64_t> executed_count_{0};
  // Set by main thread when interrupt time should be reset before next run
  std::atomic<bool> reset_time_pending_{false};

  // Colors window
  bool show_colors_window_{false};

  // Tilemap window
  bool show_tilemap_window_{false};
  int tilemap_scale_{1};
  int tilemap_palette_{};
  Tilemap tilemap_;

  // Plane A / Plane B / Window planes
  std::array<bool, kPlaneTypes> show_plane_window_{};
  std::array<int, kPlaneTypes> plane_scale_{1, 1, 1};
  std::array<Plane, kPlaneTypes> planes_;

  // Sprite table window
  bool show_sprite_table_window_{false};
  bool sprite_table_auto_update_{false};
  int sprite_scale_{1};
  std::span<const Sprite> sprites_;
  std::span<const ImTextureID> sprite_textures_;

  // Demo window
  bool show_demo_window_{false};

  // Audio
  AudioOutput audio_output_;
  std::atomic<bool> audio_enabled_{true};
};

} // namespace sega
