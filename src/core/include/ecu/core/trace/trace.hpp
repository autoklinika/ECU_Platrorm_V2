#pragma once

#include "ecu/core/runtime/command.hpp"
#include "ecu/core/time/monotonic_clock.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::core::trace {

using TraceSequence = std::uint64_t;

enum class TraceCategory : std::uint8_t {
  core,
  lifecycle,
  resource,
  can,
  diagnostic_transport,
  isotp,
  uds,
  j1939,
  doip,
  security,
  storage,
  application,
};

enum class TraceDirection : std::uint8_t {
  none,
  rx,
  tx,
};

enum class TraceSeverity : std::uint8_t {
  debug,
  info,
  warning,
  error,
  critical,
};

struct TraceRecordHeader {
  TraceSequence sequence{0U};
  time::MonotonicTime timestamp{0};
  TraceCategory category{TraceCategory::core};
  TraceDirection direction{TraceDirection::none};
  TraceSeverity severity{TraceSeverity::info};
  std::uint32_t subject_id{0U};
  runtime::CorrelationId correlation_id{0U};
  bool truncated{false};
};

struct TraceRecordView {
  TraceRecordHeader header{};
  const std::byte* payload{nullptr};
  std::size_t payload_size{0U};
};

class ITraceSink {
 public:
  virtual ~ITraceSink() = default;
  virtual void record(const TraceRecordView& record) noexcept = 0;
};

class NullTraceSink final : public ITraceSink {
 public:
  void record(const TraceRecordView&) noexcept override {}
};

}  // namespace ecu::core::trace
