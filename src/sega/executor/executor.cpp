#include "executor.h"
#include "interrupt_handler.h"
#include "lib/common/error/error.h"
#include "lib/common/memory/types.h"
#include "lib/m68k/instruction/instruction.h"
#include "lib/m68k/registers/registers.h"
#include "lib/sega/executor/presets.h"
#include "lib/sega/memory/bus_device.h"
#include "lib/sega/memory/controller_device.h"
#include "lib/sega/memory/m68k_ram_device.h"
#include "lib/sega/memory/psg_device.h"
#include "lib/sega/memory/rom_device.h"
#include "lib/sega/memory/sram_access_register_device.h"
#include "lib/sega/memory/stub_memory_device.h"
#include "lib/sega/memory/trademark_register_device.h"
#include "lib/sega/memory/vdp_device.h"
#include "lib/sega/memory/ym2612_device.h"
#include "lib/sega/memory/z80_device.h"
#include "lib/sega/rom_loader/rom_loader.h"
#include "lib/sega/state_dump/state_dump.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>
#include <vector>

namespace sega {

class Executor::Impl {
public:
  Impl(const Impl&) = delete;
  Impl(Impl&&) = delete;

  Impl(std::string_view rom_path)
      : rom_{load_rom(rom_path)}, rom_device_{DataView{reinterpret_cast<const Byte*>(rom_.data()), rom_.size()}},
        vdp_device_{bus_}, interrupt_handler_{vector_table().vblank_pc.get(), registers_, bus_, vdp_device_},
        state_dump_{vdp_device_, m68k_ram_device_, z80_ram_device_, registers_} {
    spdlog::info("loaded ROM file {}", rom_path);

    // setup bus devices
    const auto rom_address = metadata().rom_address;
    bus_.add_device({rom_address.begin.get(), rom_address.end.get()}, &rom_device_);
    bus_.add_device(&z80_ram_device_);
    bus_.add_device(&ym2612_device_);
    bus_.add_device(&controller_device_);
    bus_.add_device(&z80_controller_device_);
    bus_.add_device(&trademark_register_device_);
    bus_.add_device(&sram_access_register_device_);
    bus_.add_device(&vdp_device_);
    bus_.add_device(&psg_device_);
    bus_.add_device(&m68k_ram_device_);

    // apply per-cartridge preset (extra memory regions, quirks)
    apply_preset();

    // make registers
    reset_registers();
  }

  void apply_preset() {
    const auto& serial = metadata().serial_number;
    const auto* preset = find_preset(std::string_view{serial.data(), serial.size()});
    if (!preset) {
      spdlog::info("no preset for serial '{:.{}}'", serial.data(), serial.size());
      return;
    }
    spdlog::info("applying preset: {}", preset->name);
    for (const auto& range : preset->extra_memory) {
      auto device = std::make_unique<StubMemoryDevice>(range.begin, range.end);
      bus_.add_device({range.begin, range.end}, device.get());
      stub_devices_.push_back(std::move(device));
      spdlog::info("  mapped stub memory [{:06x}, {:06x}]", range.begin, range.end);
    }
  }

  void reset() {
    m68k_ram_device_.clear();
    reset_registers();
    interrupt_handler_.reset_time();
    spdlog::info("reset");
  }

  void reset_registers() {
    // M68K reset: SR = $2700 (supervisor + IPL=7), SSP from $0, PC from $4.
    std::memset(&registers_, 0, sizeof(registers_));
    registers_.ssp = vector_table().reset_sp.get();
    registers_.pc = vector_table().reset_pc.get();
    registers_.sr.supervisor = true;
    registers_.sr.interrupt_mask = 7;
  }

  [[nodiscard]] std::expected<Executor::Result, Error> execute_single_instruction() {
    // check if interrupt happened
    auto interrupt_check = interrupt_handler_.check();
    if (!interrupt_check.has_value()) {
      spdlog::error("interrupt error");
      return std::unexpected{std::move(interrupt_check.error())};
    }
    if (interrupt_check.value()) {
      return Executor::Result::VblankInterrupt;
    }

    // decode and execute the current instruction
    const auto begin_pc = registers_.pc;
    auto inst = m68k::Instruction::decode({.registers = registers_, .device = bus_});
    assert(inst);
    if (auto err = inst->execute({.registers = registers_, .device = bus_})) {
      spdlog::error("execute error pc: {:06x} what: {}", begin_pc, err->what());
      return std::unexpected{std::move(*err)};
    }
    return Executor::Result::Executed;
  }

  void set_game_speed(double game_speed) {
    interrupt_handler_.set_game_speed(game_speed);
  }

  void reset_interrupt_time() {
    interrupt_handler_.reset_time();
  }

  Executor::InstructionInfo current_instruction_info() {
    // print current instruction, therefore double-fetching it, so need to restore PC
    const auto begin_pc = registers_.pc;
    auto inst = m68k::Instruction::decode({.registers = registers_, .device = bus_});
    assert(inst);
    const auto end_pc = registers_.pc;
    registers_.pc = begin_pc;

    return {.pc = begin_pc,
            .bytes = DataView{reinterpret_cast<const Byte*>(rom_.data() + begin_pc), end_pc - begin_pc},
            .description = inst->print()};
  }

  ControllerDevice& controller_device() {
    return controller_device_;
  }

  const VdpDevice& vdp_device() const {
    return vdp_device_;
  }

  const VectorTable& vector_table() const {
    return rom_header().vector_table;
  }

  const Metadata& metadata() const {
    return rom_header().metadata;
  }

  const m68k::Registers& registers() const {
    return registers_;
  }

  void save_dump_to_file(std::string_view path) const {
    StateDump::save_dump_to_file(vdp_device_, path);
  }

  void apply_dump_from_file(std::string_view path) {
    StateDump::apply_dump_from_file(vdp_device_, path);
  }

  void save_state_to_file(std::string_view path) const {
    state_dump_.save_state_to_file(path);
  }

  void load_state_from_file(std::string_view path) {
    state_dump_.load_state_from_file(path);
    interrupt_handler_.reset_time();
  }

  void generate_frame_audio(int16_t* out, size_t frame_count) {
    // Generate PSG samples into `out`
    psg_device_.generate(out, frame_count);

    // Generate YM2612 samples and add into `out`
    static thread_local std::vector<int16_t> ym_buf;
    ym_buf.resize(frame_count * 2);
    ym2612_device_.generate(ym_buf.data(), frame_count);

    for (size_t i = 0; i < frame_count * 2; ++i) {
      const int32_t mixed = static_cast<int32_t>(out[i]) + static_cast<int32_t>(ym_buf[i]);
      out[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
    }
  }

private:
  const Header& rom_header() const {
    return *reinterpret_cast<const Header*>(rom_.data());
  }

private:
  // ROM content
  const std::vector<char> rom_;

  // memory devices
  BusDevice bus_;
  RomDevice rom_device_;
  Z80RamDevice z80_ram_device_;
  Ym2612Device ym2612_device_;
  ControllerDevice controller_device_;
  Z80ControllerDevice z80_controller_device_;
  SramAccessRegisterDevice sram_access_register_device_;
  TrademarkRegisterDevice trademark_register_device_;
  VdpDevice vdp_device_;
  PsgDevice psg_device_;
  M68kRamDevice m68k_ram_device_;
  std::vector<std::unique_ptr<StubMemoryDevice>> stub_devices_;

  // registers
  m68k::Registers registers_;

  // interrupt handler
  InterruptHandler interrupt_handler_;

  // utils
  StateDump state_dump_;
};

Executor::Executor(std::string_view rom_path) : impl_{std::make_unique<Impl>(rom_path)} {}

Executor::~Executor() = default;

[[nodiscard]] std::expected<Executor::Result, Error> Executor::execute_current_instruction() {
  return impl_->execute_single_instruction();
}

void Executor::set_game_speed(double game_speed) {
  impl_->set_game_speed(game_speed);
}

void Executor::reset_interrupt_time() {
  impl_->reset_interrupt_time();
}

void Executor::reset() {
  impl_->reset();
}

Executor::InstructionInfo Executor::current_instruction_info() {
  return impl_->current_instruction_info();
}

ControllerDevice& Executor::controller_device() {
  return impl_->controller_device();
}

const VdpDevice& Executor::vdp_device() const {
  return impl_->vdp_device();
}

const VectorTable& Executor::vector_table() const {
  return impl_->vector_table();
}

const Metadata& Executor::metadata() const {
  return impl_->metadata();
}

const m68k::Registers& Executor::registers() const {
  return impl_->registers();
}

void Executor::save_dump_to_file(std::string_view path) const {
  return impl_->save_dump_to_file(path);
}

void Executor::apply_dump_from_file(std::string_view path) {
  return impl_->apply_dump_from_file(path);
}

void Executor::save_state_to_file(std::string_view path) const {
  return impl_->save_state_to_file(path);
}

void Executor::load_state_from_file(std::string_view path) {
  return impl_->load_state_from_file(path);
}

void Executor::generate_frame_audio(int16_t* out, size_t frame_count) {
  impl_->generate_frame_audio(out, frame_count);
}

} // namespace sega
