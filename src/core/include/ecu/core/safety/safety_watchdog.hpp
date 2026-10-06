#pragma once

#include "ecu/core/time/monotonic_clock.hpp"

#include <chrono>
#include <cstdint>

namespace ecu::core::safety {

enum class WatchdogState : std::uint8_t {
  disarmed,
  armed,
  expired,
};

enum class WatchdogStatus : std::uint8_t {
  ok,
  invalid_argument,
  not_armed,
  expired,
};

class SafetyWatchdog {
 public:
  explicit SafetyWatchdog(
      const time::IMonotonicClock& clock) noexcept;

  WatchdogStatus arm(std::chrono::nanoseconds timeout) noexcept;
  WatchdogStatus kick() noexcept;
  void disarm() noexcept;

  [[nodiscard]] WatchdogStatus poll() noexcept;
  [[nodiscard]] WatchdogState state() const noexcept;
  [[nodiscard]] time::MonotonicTime deadline() const noexcept;

 private:
  const time::IMonotonicClock& clock_;
  WatchdogState state_{WatchdogState::disarmed};
  std::chrono::nanoseconds timeout_{0};
  time::MonotonicTime deadline_{0};
};

}  // namespace ecu::core::safety
