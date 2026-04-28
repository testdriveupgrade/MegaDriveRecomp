# Build Guide

## Requirements

### Compiler

**Clang** with `libc++` is required. GCC is not supported — the CMake config explicitly sets `clang++` and links against `libc++`/`libc++abi`.

- Minimum: Clang 18+ (C++26 support needed for the main project)
- The output sub-project (`output/`) uses C++23

### System packages

**Linux (Ubuntu/Debian):**
```sh
sudo apt-get install \
  cmake ninja-build clang libc++-dev libc++abi-dev \
  git pkg-config python3-jinja2 \
  libwayland-dev mesa-common-dev xorg-dev libxkbcommon-dev
```

**Windows:**
- Install [LLVM/Clang](https://github.com/llvm/llvm-project/releases) (include `libc++`)
- Install [CMake](https://cmake.org/download/) >= 3.22
- Install [Ninja](https://ninja-build.org/)
- Install [Git](https://git-scm.com/)
- OpenGL is provided by the GPU driver

**macOS:**
```sh
brew install cmake ninja llvm
```
Then set `CC`/`CXX` to the Homebrew Clang, not Apple Clang:
```sh
export CC=$(brew --prefix llvm)/bin/clang
export CXX=$(brew --prefix llvm)/bin/clang++
```

---

## External dependencies

All external libraries are **downloaded automatically** by CMake via `FetchContent` — no manual installation needed.

| Library | Version | Purpose |
|---------|---------|---------|
| [fmt](https://github.com/fmtlib/fmt) | 11.0.2 | String formatting |
| [spdlog](https://github.com/gabime/spdlog) | 1.14.1 | Logging |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | Window and input |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91.4 | Debug UI |
| [GLAD](https://github.com/Dav1dde/glad) | v2.0.8 | OpenGL loader |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON parsing |
| [magic_enum](https://github.com/Neargye/magic_enum) | 0.9.6 | Compile-time enum reflection |
| [stb](https://github.com/nothings/stb) | latest | Image read/write |
| [miniaudio](https://github.com/mackron/miniaudio) | latest | Audio output |
| [Nuked-OPN2](https://github.com/nukeykt/Nuked-OPN2) | latest | YM2612 FM emulation |

---

## Build

### Main project (emulator + recompiler tools)

```sh
cmake -S src -B build -G Ninja
cmake --build build
```

Binaries are placed in `build/bin/`:
- `sega_emulator` — Sega Genesis emulator
- `smd_recomp` — static recompiler tool
- `m68k_emulator` — standalone M68K emulator
- `m68k_test` — M68K instruction tests
- `sega_video_test` — video rendering test

### Output project (recompiled game binary)

First run `smd_recomp` to generate the `output/func_*.cpp` files, then:

```sh
cmake -S output -B output/build -G Ninja -DROM_PATH=/path/to/sonic.gen
cmake --build output/build
```

The `ROM_PATH` is embedded at compile time for runtime sprite extraction. Defaults to `sonic.gen` next to the `output/` directory.

---

## Debug build

Edit `src/CMakeLists.txt` and swap the build type lines:

```cmake
# set(CMAKE_BUILD_TYPE Release)   ← comment out
set(CMAKE_BUILD_TYPE Debug)       ← uncomment
```

Also uncomment the sanitizer flags if needed:
```cmake
add_compile_options(-fsanitize=undefined,address)
add_link_options(-fsanitize=undefined,address)
```

---

## Running the emulator

```sh
./build/bin/sega_emulator/sega_emulator sonic.gen
```
