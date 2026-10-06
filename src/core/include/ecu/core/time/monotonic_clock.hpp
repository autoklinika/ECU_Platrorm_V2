#pragma once

#include <chrono>

namespace ecu::core::time {

using MonotonicTime = std::chrono::nanoseconds;

class IMonotonicClock {
 public:
  virtual ~IMonotonicClock() = default;

  [[nodiscard]] virtual MonotonicTime now() const noexcept = 0;
};

}  // namespace ecu::core::time
