#pragma once

#include "ecu/core/time/monotonic_clock.hpp"

#include <cstdint>

namespace ecu::core::runtime {

using StateRevision = std::uint64_t;

struct StateHeader {
  StateRevision revision{0U};
  time::MonotonicTime updated_at{0};
};

[[nodiscard]] constexpr StateRevision next_state_revision(
    const StateRevision current) noexcept {
  return current == UINT64_MAX ? UINT64_MAX : current + 1U;
}

}  // namespace ecu::core::runtime
