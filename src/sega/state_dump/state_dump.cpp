#include "state_dump.h"
#include "lib/common/memory/types.h"
#include "lib/m68k/registers/registers.h"
#include "lib/sega/memory/m68k_ram_device.h"
#include "lib/sega/memory/vdp_device.h"
#include "lib/sega/memory/z80_device.h"
#include "spdlog/spdlog.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace sega {

namespace {

constexpr char kStateMagic[8] = {'S', 'M', 'D', 'S', 'T', '0', '0', '1'};

template<typename T>
void write_pod(std::ofstream& f, const T& value) {
  f.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void write_chunk(std::ofstream& f, DataView data) {
  const uint32_t size = static_cast<uint32_t>(data.size());
  write_pod(f, size);
  f.write(reinterpret_cast<const char*>(data.data()), size);
}

template<typename T>
bool read_pod(std::ifstream& f, T& value) {
  return static_cast<bool>(f.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

bool read_chunk(std::ifstream& f, std::vector<Byte>& out) {
  uint32_t size = 0;
  if (!read_pod(f, size)) {
    return false;
  }
  out.resize(size);
  return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size));
}

} // namespace

StateDump::StateDump(VdpDevice& vdp_device, M68kRamDevice& m68k_ram_device, Z80RamDevice& z80_ram_device,
                     m68k::Registers& registers)
    : vdp_device_{vdp_device}, m68k_ram_device_{m68k_ram_device}, z80_ram_device_{z80_ram_device},
      registers_{registers} {}

void StateDump::save_dump_to_file(const VdpDevice& vdp_device, std::string_view path) {
  std::ofstream file{path.data(), std::ios::binary};
  const auto dump = vdp_device.dump_state({});
  file.write(reinterpret_cast<const char*>(dump.data()), dump.size());
  spdlog::info("save dump to file: {}", path);
}

void StateDump::apply_dump_from_file(VdpDevice& vdp_device, std::string_view path) {
  spdlog::info("read dump from file: {}", path);
  std::ifstream file{path.data(), std::ios::binary};
  std::vector<char> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  vdp_device.apply_state({}, {reinterpret_cast<Byte*>(data.data()), data.size()});
}

void StateDump::save_state_to_file(std::string_view path) const {
  std::ofstream file{path.data(), std::ios::binary | std::ios::trunc};
  if (!file) {
    spdlog::error("save state: failed to open file: {}", path);
    return;
  }

  // header
  file.write(kStateMagic, sizeof(kStateMagic));

  // M68K registers
  const DataView regs_view{reinterpret_cast<const Byte*>(&registers_), sizeof(registers_)};
  write_chunk(file, regs_view);

  // M68K RAM
  write_chunk(file, m68k_ram_device_.dump_state({}));

  // Z80 RAM
  write_chunk(file, z80_ram_device_.dump_state({}));

  // VDP state (registers + VRAM + VSRAM + CRAM)
  const auto vdp_state = vdp_device_.dump_state({});
  write_chunk(file, DataView{vdp_state.data(), vdp_state.size()});

  spdlog::info("save state to file: {}", path);
}

void StateDump::load_state_from_file(std::string_view path) {
  std::ifstream file{path.data(), std::ios::binary};
  if (!file) {
    spdlog::error("load state: failed to open file: {}", path);
    return;
  }

  // header
  char magic[sizeof(kStateMagic)] = {};
  if (!file.read(magic, sizeof(magic)) || std::memcmp(magic, kStateMagic, sizeof(kStateMagic)) != 0) {
    spdlog::error("load state: bad magic in file: {}", path);
    return;
  }

  std::vector<Byte> buffer;

  // M68K registers
  if (!read_chunk(file, buffer)) {
    spdlog::error("load state: failed to read registers chunk");
    return;
  }
  if (buffer.size() != sizeof(registers_)) {
    spdlog::error("load state: registers chunk size mismatch: got {}, expected {}", buffer.size(), sizeof(registers_));
    return;
  }
  std::memcpy(&registers_, buffer.data(), sizeof(registers_));

  // M68K RAM
  if (!read_chunk(file, buffer)) {
    spdlog::error("load state: failed to read M68K RAM chunk");
    return;
  }
  m68k_ram_device_.apply_state({}, DataView{buffer.data(), buffer.size()});

  // Z80 RAM
  if (!read_chunk(file, buffer)) {
    spdlog::error("load state: failed to read Z80 RAM chunk");
    return;
  }
  z80_ram_device_.apply_state({}, DataView{buffer.data(), buffer.size()});

  // VDP state
  if (!read_chunk(file, buffer)) {
    spdlog::error("load state: failed to read VDP chunk");
    return;
  }
  vdp_device_.apply_state({}, DataView{buffer.data(), buffer.size()});

  spdlog::info("load state from file: {}", path);
}

} // namespace sega
