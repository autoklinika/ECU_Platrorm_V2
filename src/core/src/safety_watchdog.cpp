#include "ecu/core/safety/safety_watchdog.hpp"

namespace ecu::core::safety {

SafetyWatchdog::SafetyWatchdog(
    const time::IMonotonicClock& clock) noexcept
    : clock_(clock) {}

WatchdogStatus SafetyWatchdog::arm(
    const std::chrono::nanoseconds timeout) noexcept {
  if (timeout.count() <= 0) {
    return WatchdogStatus::invalid_argument;
  }

  timeout_ = timeout;
  deadline_ = clock_.now() + timeout_;
  state_ = WatchdogState::armed;
  return WatchdogStatus::ok;
}

WatchdogStatus SafetyWatchdog::kick() noexcept {
  if (state_ != WatchdogState::armed) {
    return state_ == WatchdogState::expired
               ? WatchdogStatus::expired
               : WatchdogStatus::not_armed;
  }

  deadline_ = clock_.now() + timeout_;
  return WatchdogStatus::ok;
}

void SafetyWatchdog::disarm() noexcept {
  state_ = WatchdogState::disarmed;
  timeout_ = std::chrono::nanoseconds{0};
  deadline_ = time::MonotonicTime{0};
}

WatchdogStatus SafetyWatchdog::poll() noexcept {
  if (state_ == WatchdogState::disarmed) {
    return WatchdogStatus::not_armed;
  }

  if (state_ == WatchdogState::expired) {
    return WatchdogStatus::expired;
  }

  if (clock_.now() >= deadline_) {
    state_ = WatchdogState::expired;
    return WatchdogStatus::expired;
  }

  return WatchdogStatus::ok;
}

WatchdogState SafetyWatchdog::state() const noexcept {
  return state_;
}

time::MonotonicTime SafetyWatchdog::deadline() const noexcept {
  return deadline_;
}

}  // namespace ecu::core::safety
