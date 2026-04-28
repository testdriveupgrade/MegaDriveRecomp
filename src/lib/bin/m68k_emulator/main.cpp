#include "lib/common/error/error.h"
#include "lib/common/memory/device.h"
#include "lib/common/memory/types.h"
#include "lib/m68k/instruction/instruction.h"
#include "lib/m68k/registers/registers.h"
#include "spdlog/spdlog.h"
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

class EmulatorDevice : public Device {
public:
  EmulatorDevice(std::string_view path) {
    std::ifstream fin{path.data(), std::ios::binary};
    std::string str(std::istreambuf_iterator<char>{fin}, {});
    for (std::size_t i = 0; i < str.size(); ++i) {
      data_[i] = str[i];
    }
    spdlog::info("loaded binary: {} size: {} bytes", path, data_.size());

    std::ignore = Device::write<Long>(0xFF0000, 1307);
    std::ignore = Device::write<Long>(0xFF0004, 1320);
  }

private:
  std::optional<Error> read(AddressType addr, MutableDataView data) override {
    spdlog::info("read address: {:x} size: {}", addr, data.size());
    for (int i = 0; i < data.size(); ++i) {
      data[i] = data_[addr + i];
    }
    return std::nullopt;
  }

  std::optional<Error> write(AddressType addr, DataView data) override {
    spdlog::info("write address: {:x} size: {}", addr, data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
      data_[addr + i] = data[i];
    }
    if (addr == 0xFF0008) {
      spdlog::info(" ---------- wrote value {} to 0xFF0008 ----------", data.as<Long>());
    }
    return std::nullopt;
  }

private:
  std::map<std::size_t, Byte> data_;
};

int main(int argc, char** argv) {
  EmulatorDevice device{argv[1]};

  // boot from ROM vector table: [0..3] = initial SSP, [4..7] = reset PC
  m68k::Registers regs;
  regs.ssp = device.Device::read<Long>(0).value_or(0);
  regs.pc  = device.Device::read<Long>(4).value_or(0);
  regs.sr.supervisor = true;
  spdlog::info("boot: SSP={:08x} PC={:08x}", regs.ssp, regs.pc);

  while (true) {
    const auto begin_pc = regs.pc;
    auto inst = m68k::Instruction::decode({.registers = regs, .device = device});
    if (!inst) {
      break;
    }
    if (auto err = inst->execute({.registers = regs, .device = device})) {
      spdlog::error("execute error pc: {:06x} what: {}", begin_pc, err->what());
      break;
    }
  }
}
