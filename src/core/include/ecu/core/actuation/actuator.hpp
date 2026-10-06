#pragma once

#include "ecu/core/runtime/command_dispatcher.hpp"
#include "ecu/core/runtime/lifecycle.hpp"

#include <cstdint>

namespace ecu::core::actuation {

using ActuatorId = std::uint32_t;
using ActuatorCapabilityMask = std::uint64_t;

enum class ActuatorState : std::uint8_t {
  disabled,
  ready,
  active,
  faulted,
};

enum class ActuatorStatus : std::uint8_t {
  ok,
  in_progress,
  busy,
  invalid_argument,
  not_ready,
  interlocked,
  fault,
  transport_error,
};

struct ActuatorDescriptor {
  ActuatorId id{0U};
  ActuatorCapabilityMask capabilities{0U};
};

class IActuator : public runtime::ILifecycleComponent {
 public:
  ~IActuator() override = default;

  [[nodiscard]] virtual ActuatorDescriptor descriptor() const noexcept = 0;
  [[nodiscard]] virtual ActuatorState actuator_state() const noexcept = 0;

  [[nodiscard]] virtual ActuatorStatus execute(
      const runtime::CommandView& command) noexcept = 0;

  virtual ActuatorStatus safe_stop() noexcept = 0;
};

}  // namespace ecu::core::actuation
