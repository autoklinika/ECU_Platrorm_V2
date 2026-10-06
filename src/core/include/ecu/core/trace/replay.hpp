#pragma once

#include "ecu/core/trace/trace.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::core::trace {

enum class ReplayStatus : std::uint8_t {
  ok,
  end_of_stream,
  invalid_argument,
  buffer_too_small,
  io_error,
};

class IReplaySource {
 public:
  virtual ~IReplaySource() = default;

  virtual ReplayStatus next(
      TraceRecordHeader& header,
      std::byte* payload,
      std::size_t capacity,
      std::size_t& payload_size) noexcept = 0;

  virtual void reset() noexcept = 0;
};

}  // namespace ecu::core::trace
