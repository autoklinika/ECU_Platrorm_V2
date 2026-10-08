#pragma once

#include "ecu/core/time/monotonic_clock.hpp"

#include <cstdint>

namespace ecu::core::runtime {

using CommandId = std::uint64_t;
using CorrelationId = std::uint64_t;
using CommandTypeId = std::uint32_t;

enum class CommandPriority : std::uint8_t {
  normal,
  high,
  safety_critical,
};

enum class CommandOutcome : std::uint8_t {
  accepted,
  rejected,
  running,
  succeeded,
  failed,
  cancelled,
};

struct CommandHeader {
  CommandId id{0U};
  CommandTypeId type{0U};
  CorrelationId correlation_id{0U};
  CommandPriority priority{CommandPriority::normal};
  time::MonotonicTime issued_at{0};
};

struct CommandResult {
  CommandOutcome outcome{CommandOutcome::rejected};
  std::uint32_t error_code{0U};
};

[[nodiscard]] constexpr bool is_valid_command_header(
    const CommandHeader& header) noexcept {
  return header.id != 0U && header.type != 0U;
}

}  // namespace ecu::core::runtime
