#pragma once

#include <atomic>

namespace ecu::core::runtime {

class CancellationFlag {
 public:
  void request() noexcept {
    requested_.store(true, std::memory_order_release);
  }

  void reset() noexcept {
    requested_.store(false, std::memory_order_release);
  }

  [[nodiscard]] bool requested() const noexcept {
    return requested_.load(std::memory_order_acquire);
  }

 private:
  std::atomic<bool> requested_{false};
};

}  // namespace ecu::core::runtime
