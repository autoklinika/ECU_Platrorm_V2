#include "ecu/core/runtime/module_registry.hpp"

namespace ecu::core::runtime {

ModuleRegistrationStatus ModuleRegistry::register_module(
    ICoreModule& module) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  const auto descriptor = module.descriptor();
  if (descriptor.id == 0U) {
    return ModuleRegistrationStatus::invalid_argument;
  }

  ICoreModule** free_slot = nullptr;

  for (auto& existing : modules_) {
    if (existing == &module) {
      return ModuleRegistrationStatus::already_registered;
    }

    if (existing != nullptr &&
        existing->descriptor().id == descriptor.id) {
      return ModuleRegistrationStatus::id_conflict;
    }

    if (existing == nullptr && free_slot == nullptr) {
      free_slot = &existing;
    }
  }

  if (free_slot == nullptr) {
    return ModuleRegistrationStatus::capacity_exhausted;
  }

  *free_slot = &module;
  return ModuleRegistrationStatus::registered;
}

bool ModuleRegistry::unregister_module(
    ICoreModule& module) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (auto& existing : modules_) {
    if (existing == &module) {
      existing = nullptr;
      return true;
    }
  }

  return false;
}

ICoreModule* ModuleRegistry::find(
    const ModuleId id) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (auto* module : modules_) {
    if (module != nullptr &&
        module->descriptor().id == id) {
      return module;
    }
  }

  return nullptr;
}

const ICoreModule* ModuleRegistry::find(
    const ModuleId id) const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (const auto* module : modules_) {
    if (module != nullptr &&
        module->descriptor().id == id) {
      return module;
    }
  }

  return nullptr;
}

std::size_t ModuleRegistry::module_count() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  std::size_t count = 0U;
  for (const auto* module : modules_) {
    if (module != nullptr) {
      ++count;
    }
  }
  return count;
}

}  // namespace ecu::core::runtime
