#pragma once

#include "ecu/core/runtime/command.hpp"
#include "ecu/core/time/monotonic_clock.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::core::runtime {

using EventSequence = std::uint64_t;
using EventTypeId = std::uint32_t;

enum class EventSeverity : std::uint8_t {
  debug,
  info,
  warning,
  error,
  critical,
};

struct EventHeader {
  EventSequence sequence{0U};
  EventTypeId type{0U};
  CorrelationId correlation_id{0U};
  EventSeverity severity{EventSeverity::info};
  time::MonotonicTime timestamp{0};
};

struct EventView {
  EventHeader header{};
  const std::byte* payload{nullptr};
  std::size_t payload_size{0U};
};

class IEventSink {
 public:
  virtual ~IEventSink() = default;
  virtual void publish(const EventView& event) noexcept = 0;
};

class NullEventSink final : public IEventSink {
 public:
  void publish(const EventView&) noexcept override {}
};

}  // namespace ecu::core::runtime
