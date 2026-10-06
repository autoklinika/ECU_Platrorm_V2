#include "ecu/core/runtime/lifecycle.hpp"

namespace ecu::core::runtime {

LifecycleState LifecycleStateMachine::state() const noexcept {
  return state_;
}

LifecycleStatus LifecycleStateMachine::begin_start() noexcept {
  if (state_ != LifecycleState::stopped) {
    return LifecycleStatus::invalid_transition;
  }

  state_ = LifecycleState::starting;
  return LifecycleStatus::ok;
}

LifecycleStatus LifecycleStateMachine::mark_running() noexcept {
  if (state_ != LifecycleState::starting) {
    return LifecycleStatus::invalid_transition;
  }

  state_ = LifecycleState::running;
  return LifecycleStatus::ok;
}

LifecycleStatus LifecycleStateMachine::begin_stop() noexcept {
  if (state_ != LifecycleState::running &&
      state_ != LifecycleState::starting) {
    return LifecycleStatus::invalid_transition;
  }

  state_ = LifecycleState::stopping;
  return LifecycleStatus::ok;
}

LifecycleStatus LifecycleStateMachine::mark_stopped() noexcept {
  if (state_ != LifecycleState::stopping) {
    return LifecycleStatus::invalid_transition;
  }

  state_ = LifecycleState::stopped;
  return LifecycleStatus::ok;
}

void LifecycleStateMachine::mark_faulted() noexcept {
  state_ = LifecycleState::faulted;
}

LifecycleStatus LifecycleStateMachine::reset_fault() noexcept {
  if (state_ != LifecycleState::faulted) {
    return LifecycleStatus::invalid_transition;
  }

  state_ = LifecycleState::stopped;
  return LifecycleStatus::ok;
}

}  // namespace ecu::core::runtime
