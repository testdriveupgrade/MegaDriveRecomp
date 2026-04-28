#include "controller_device.h"
#include "fmt/format.h"
#include "lib/common/error/error.h"
#include "lib/common/memory/types.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <utility>

// reference: https://wiki.megadrive.org/index.php?title=IO_Registers

namespace sega {

namespace {

// even addresses are no-op, just for 2-bytes padding
constexpr AddressType kVersion = 0xA10001;

constexpr AddressType kData1 = 0xA10003;
constexpr AddressType kData2 = 0xA10005;
constexpr AddressType kDataExt = 0xA10007;

constexpr AddressType kCtrl1 = 0xA10009;
constexpr AddressType kCtrl2 = 0xA1000B;
constexpr AddressType kCtrlExt = 0xA1000D;

constexpr AddressType kSerialTransmit1 = 0xA1000F;
constexpr AddressType kSerialReceive1 = 0xA10011;
constexpr AddressType kSerialControl1 = 0xA10013;

constexpr AddressType kSerialTransmit2 = 0xA10015;
constexpr AddressType kSerialReceive2 = 0xA10017;
constexpr AddressType kSerialControl2 = 0xA10019;

constexpr AddressType kSerialTransmitExt = 0xA1001B;
constexpr AddressType kSerialReceiveExt = 0xA1001D;
constexpr AddressType kSerialControlExt = 0xA1001F;

struct Version {
  enum class ExpansionUnitStatus : uint8_t {
    Connected = 0,
    NotConnected = 1,
  };

  enum class Clock : uint8_t {
    NTSC = 0,
    PAL = 1,
  };

  enum class Model : uint8_t {
    Domestic = 0, // Japanese
    Overseas = 1, // US or European
  };

  uint8_t version_number : 4;
  bool : 1;
  ExpansionUnitStatus expansion_unit_status : 1;
  Clock clock : 1;
  Model model : 1;
};
static_assert(sizeof(Version) == 1);

struct Step1Value {
  bool up : 1;
  bool down : 1;
  bool left : 1;
  bool right : 1;
  bool b : 1;
  bool c : 1;
};
static_assert(sizeof(Step1Value) == 1);

struct Step2Value {
  bool up : 1;
  bool down : 1;
  bool : 2;
  bool a : 1;
  bool start : 1;
};
static_assert(sizeof(Step2Value) == 1);

} // namespace

void ControllerDevice::set_button(Button button, bool pressed) {
  const uint8_t mask = static_cast<uint8_t>(1u << std::to_underlying(button));
  if (pressed) {
    buttons0_.fetch_or(mask, std::memory_order_relaxed);
  } else {
    buttons0_.fetch_and(static_cast<uint8_t>(~mask), std::memory_order_relaxed);
  }
}

std::optional<Error> ControllerDevice::read(AddressType addr, MutableDataView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    auto& value = data[i];
    switch (addr + i) {
    // version register
    case kVersion:
      value = read_version();
      break;
    // data registers
    case kData1:
      value = read_pressed_status(0);
      break;
    case kData2:
      value = read_pressed_status(1);
      break;
    case kDataExt:
      value = read_pressed_status(2);
      break;
    // control registers
    case kCtrl1:
      value = ctrl_value_[0];
      break;
    case kCtrl2:
      value = ctrl_value_[1];
      break;
    case kCtrlExt:
      value = ctrl_value_[2];
      break;
    // no-op, write zero
    default:
      value = 0x00;
    }
  }
  return std::nullopt;
}

std::optional<Error> ControllerDevice::write(AddressType addr, DataView data) {
  for (size_t i = 0; i < data.size(); ++i) {
    const auto value = data[i];
    switch (addr + i) {
    // data registers
    case kData1:
      current_step_by_controller_[0] = value == 0x40 ? StepNumber::Step1 : StepNumber::Step2;
      break;
    case kData2:
      current_step_by_controller_[1] = value == 0x40 ? StepNumber::Step1 : StepNumber::Step2;
      break;
    case kDataExt:
      current_step_by_controller_[2] = value == 0x40 ? StepNumber::Step1 : StepNumber::Step2;
      break;
    // control registers
    case kCtrl1:
      ctrl_value_[0] = value;
      break;
    case kCtrl2:
      ctrl_value_[1] = value;
      break;
    case kCtrlExt:
      ctrl_value_[2] = value;
      break;
    // serial registers, no-op
    case kSerialControl1:
    case kSerialControl2:
    case kSerialControlExt:
      break;
    default:
      spdlog::error("Invalid controller write address: {:06x} data: {:02x}", addr + i, data[i]);
    }
  }
  return std::nullopt;
}

Byte ControllerDevice::read_version() {
  static constexpr auto kEmulatorVersion = Version{
      .version_number = 0xF,
      .expansion_unit_status = Version::ExpansionUnitStatus::NotConnected,
      .clock = Version::Clock::NTSC,
      .model = Version::Model::Overseas,
  };
  const auto as_byte = std::bit_cast<Byte>(kEmulatorVersion);
  spdlog::debug("read version: {:02x}", as_byte);
  return as_byte;
}

Byte ControllerDevice::read_pressed_status(size_t controller) {
  // Only controller 0 has live input; controllers 1 and 2 always report unpressed.
  const uint8_t buttons = (controller == 0) ? buttons0_.load(std::memory_order_relaxed) : 0u;
  auto btn = [&](Button b) { return (buttons >> std::to_underlying(b)) & 1u; };

  const auto& current_step = current_step_by_controller_[controller];
  switch (current_step) {
  case StepNumber::Step1: {
    Step1Value value;
    std::memset(&value, 0, sizeof(value));
    value.up    = !btn(Button::Up);
    value.down  = !btn(Button::Down);
    value.left  = !btn(Button::Left);
    value.right = !btn(Button::Right);
    value.b     = !btn(Button::B);
    value.c     = !btn(Button::C);
    return std::bit_cast<Byte>(value);
  }
  case StepNumber::Step2: {
    Step2Value value;
    std::memset(&value, 0, sizeof(value));
    value.up    = !btn(Button::Up);
    value.down  = !btn(Button::Down);
    value.a     = !btn(Button::A);
    value.start = !btn(Button::Start);
    return std::bit_cast<Byte>(value);
  }
  }
}

} // namespace sega
