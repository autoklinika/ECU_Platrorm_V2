#pragma once

#include <cstdint>

namespace ecu::core::runtime {

enum class LifecycleState : std::uint8_t {
  stopped,
  starting,
  running,
  stopping,
  faulted,
};

enum class LifecycleStatus : std::uint8_t {
  ok,
  invalid_transition,
};

class LifecycleStateMachine {
 public:
  [[nodiscard]] LifecycleState state() const noexcept;

  LifecycleStatus begin_start() noexcept;
  LifecycleStatus mark_running() noexcept;
  LifecycleStatus begin_stop() noexcept;
  LifecycleStatus mark_stopped() noexcept;
  void mark_faulted() noexcept;
  LifecycleStatus reset_fault() noexcept;

 private:
  LifecycleState state_{LifecycleState::stopped};
};

class ILifecycleComponent {
 public:
  virtual ~ILifecycleComponent() = default;

  [[nodiscard]] virtual LifecycleState state() const noexcept = 0;
  virtual LifecycleStatus start() noexcept = 0;
  virtual LifecycleStatus poll() noexcept = 0;
  virtual LifecycleStatus stop() noexcept = 0;
};

}  // namespace ecu::core::runtime
