#pragma once

#include <cstdint>

namespace ecu::core::device {

using DeviceId = std::uint32_t;
using DeviceCapabilityMask = std::uint64_t;

enum class DeviceClass : std::uint8_t {
  can_interface,
  ethernet_interface,
  digital_io,
  camera,
  thermal_camera,
  printer,
  robot_arm,
  storage,
  security,
  custom,
};

struct DeviceDescriptor {
  DeviceId id{0U};
  DeviceClass device_class{DeviceClass::custom};
  DeviceCapabilityMask capabilities{0U};
};

class IDevice {
 public:
  virtual ~IDevice() = default;

  [[nodiscard]] virtual DeviceDescriptor descriptor() const noexcept = 0;
  [[nodiscard]] virtual bool available() const noexcept = 0;
};

}  // namespace ecu::core::device
