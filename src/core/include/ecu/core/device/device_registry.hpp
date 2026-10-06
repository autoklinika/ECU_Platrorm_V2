#pragma once

#include "ecu/core/device/device.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ecu::core::device {

enum class DeviceRegistrationStatus : std::uint8_t {
  registered,
  already_registered,
  id_conflict,
  invalid_argument,
  capacity_exhausted,
};

class DeviceRegistry {
 public:
  static constexpr std::size_t kMaxDevices = 128U;

  [[nodiscard]] DeviceRegistrationStatus register_device(
      IDevice& device) noexcept;

  [[nodiscard]] bool unregister_device(
      IDevice& device) noexcept;

  [[nodiscard]] IDevice* find(DeviceId id) noexcept;
  [[nodiscard]] const IDevice* find(DeviceId id) const noexcept;

  [[nodiscard]] std::size_t device_count() const noexcept;

 private:
  mutable std::mutex mutex_{};
  std::array<IDevice*, kMaxDevices> devices_{};
};

}  // namespace ecu::core::device
