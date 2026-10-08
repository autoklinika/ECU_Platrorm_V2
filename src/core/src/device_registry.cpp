#include "ecu/core/device/device_registry.hpp"

namespace ecu::core::device {

DeviceRegistrationStatus DeviceRegistry::register_device(
    IDevice& device) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  const auto descriptor = device.descriptor();
  if (descriptor.id == 0U) {
    return DeviceRegistrationStatus::invalid_argument;
  }

  IDevice** free_slot = nullptr;

  for (auto& existing : devices_) {
    if (existing == &device) {
      return DeviceRegistrationStatus::already_registered;
    }

    if (existing != nullptr &&
        existing->descriptor().id == descriptor.id) {
      return DeviceRegistrationStatus::id_conflict;
    }

    if (existing == nullptr && free_slot == nullptr) {
      free_slot = &existing;
    }
  }

  if (free_slot == nullptr) {
    return DeviceRegistrationStatus::capacity_exhausted;
  }

  *free_slot = &device;
  return DeviceRegistrationStatus::registered;
}

bool DeviceRegistry::unregister_device(
    IDevice& device) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (auto& existing : devices_) {
    if (existing == &device) {
      existing = nullptr;
      return true;
    }
  }

  return false;
}

IDevice* DeviceRegistry::find(
    const DeviceId id) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (auto* device : devices_) {
    if (device != nullptr &&
        device->descriptor().id == id) {
      return device;
    }
  }

  return nullptr;
}

const IDevice* DeviceRegistry::find(
    const DeviceId id) const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (const auto* device : devices_) {
    if (device != nullptr &&
        device->descriptor().id == id) {
      return device;
    }
  }

  return nullptr;
}

std::size_t DeviceRegistry::device_count() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  std::size_t count = 0U;
  for (const auto* device : devices_) {
    if (device != nullptr) {
      ++count;
    }
  }

  return count;
}

}  // namespace ecu::core::device
