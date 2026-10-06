#include "ecu/core_v2/safety/deadline_watchdog.hpp"

#include <limits>

namespace ecu::core::v2::safety {

DeadlineWatchdog::DeadlineWatchdog(
    const time::IMonotonicClock& clock) noexcept
    : clock_(clock) {}

DeadlineWatchdogStatus DeadlineWatchdog::arm(
    const std::chrono::nanoseconds timeout) noexcept {
  if (timeout.count() <= 0) {
    return DeadlineWatchdogStatus::invalid_argument;
  }

  const auto now = clock_.now();
  if (now.count() > std::numeric_limits<time::MonotonicTime::rep>::max() - timeout.count()) {
    return DeadlineWatchdogStatus::invalid_argument;
  }
  timeout_ = timeout;
  deadline_ = now + timeout_;
  state_ = DeadlineWatchdogState::armed;
  return DeadlineWatchdogStatus::ok;
}

DeadlineWatchdogStatus DeadlineWatchdog::kick() noexcept {
  if (state_ == DeadlineWatchdogState::expired) {
    return DeadlineWatchdogStatus::expired;
  }

  if (state_ != DeadlineWatchdogState::armed) {
    return DeadlineWatchdogStatus::not_armed;
  }

  const auto now = clock_.now();
  if (deadline_reached(now)) {
    return DeadlineWatchdogStatus::expired;
  }

  if (now.count() > std::numeric_limits<time::MonotonicTime::rep>::max() - timeout_.count()) {
    state_ = DeadlineWatchdogState::expired;
    return DeadlineWatchdogStatus::expired;
  }
  deadline_ = now + timeout_;
  return DeadlineWatchdogStatus::ok;
}

void DeadlineWatchdog::disarm() noexcept {
  state_ = DeadlineWatchdogState::disarmed;
  timeout_ = std::chrono::nanoseconds{0};
  deadline_ = time::MonotonicTime{0};
}

DeadlineWatchdogStatus DeadlineWatchdog::poll() noexcept {
  if (state_ == DeadlineWatchdogState::expired) {
    return DeadlineWatchdogStatus::expired;
  }

  if (state_ != DeadlineWatchdogState::armed) {
    return DeadlineWatchdogStatus::not_armed;
  }

  return deadline_reached(clock_.now())
             ? DeadlineWatchdogStatus::expired
             : DeadlineWatchdogStatus::ok;
}

DeadlineWatchdogState DeadlineWatchdog::state() const noexcept {
  return state_;
}

time::MonotonicTime DeadlineWatchdog::deadline() const noexcept {
  return deadline_;
}

bool DeadlineWatchdog::deadline_reached(
    const time::MonotonicTime now) noexcept {
  if (now < deadline_) {
    return false;
  }

  state_ = DeadlineWatchdogState::expired;
  return true;
}

}  // namespace ecu::core::v2::safety
