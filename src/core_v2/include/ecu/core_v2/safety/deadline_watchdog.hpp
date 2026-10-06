#pragma once

#include "ecu/core_v2/time/monotonic_clock.hpp"

#include <chrono>
#include <cstdint>

namespace ecu::core::v2::safety {

enum class DeadlineWatchdogState : std::uint8_t {
  disarmed,
  armed,
  expired,
};

enum class DeadlineWatchdogStatus : std::uint8_t {
  ok,
  invalid_argument,
  not_armed,
  expired,
};

class DeadlineWatchdog {
 public:
  explicit DeadlineWatchdog(
      const time::IMonotonicClock& clock) noexcept;

  // Valid arm explicitly rearms from disarmed, armed or expired state.
  // Zero/negative or unrepresentable arm changes neither state nor deadline.
  // An unrepresentable kick expires fail-closed.
  [[nodiscard]] DeadlineWatchdogStatus arm(
      std::chrono::nanoseconds timeout) noexcept;
  [[nodiscard]] DeadlineWatchdogStatus kick() noexcept;
  void disarm() noexcept;

  [[nodiscard]] DeadlineWatchdogStatus poll() noexcept;
  [[nodiscard]] DeadlineWatchdogState state() const noexcept;
  [[nodiscard]] time::MonotonicTime deadline() const noexcept;

 private:
  [[nodiscard]] bool deadline_reached(
      time::MonotonicTime now) noexcept;

  const time::IMonotonicClock& clock_;
  DeadlineWatchdogState state_{DeadlineWatchdogState::disarmed};
  std::chrono::nanoseconds timeout_{0};
  time::MonotonicTime deadline_{0};
};

}  // namespace ecu::core::v2::safety
