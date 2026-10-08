#pragma once

#include "ecu/core/runtime/lifecycle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ecu::core::runtime {

using ModuleId = std::uint32_t;
using ModuleCapabilityMask = std::uint64_t;

enum class ModuleKind : std::uint8_t {
  core_service,
  protocol,
  ecu_module,
  device_service,
  application,
};

struct ModuleDescriptor {
  ModuleId id{0U};
  ModuleKind kind{ModuleKind::application};
  ModuleCapabilityMask capabilities{0U};
};

class ICoreModule : public ILifecycleComponent {
 public:
  ~ICoreModule() override = default;
  [[nodiscard]] virtual ModuleDescriptor descriptor() const noexcept = 0;
};

enum class ModuleRegistrationStatus : std::uint8_t {
  registered,
  already_registered,
  id_conflict,
  invalid_argument,
  capacity_exhausted,
};

class ModuleRegistry {
 public:
  static constexpr std::size_t kMaxModules = 64U;

  [[nodiscard]] ModuleRegistrationStatus register_module(
      ICoreModule& module) noexcept;

  [[nodiscard]] bool unregister_module(
      ICoreModule& module) noexcept;

  [[nodiscard]] ICoreModule* find(ModuleId id) noexcept;
  [[nodiscard]] const ICoreModule* find(ModuleId id) const noexcept;

  [[nodiscard]] std::size_t module_count() const noexcept;

 private:
  mutable std::mutex mutex_{};
  std::array<ICoreModule*, kMaxModules> modules_{};
};

}  // namespace ecu::core::runtime
