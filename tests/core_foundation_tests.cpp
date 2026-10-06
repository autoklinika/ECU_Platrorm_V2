#include "ecu/core/runtime/cancellation.hpp"
#include "ecu/core/runtime/command.hpp"
#include "ecu/core/runtime/event.hpp"
#include "ecu/core/runtime/lifecycle.hpp"
#include "ecu/core/runtime/resource_manager.hpp"
#include "ecu/core/runtime/state.hpp"
#include "ecu/core/storage/key_value_store.hpp"
#include "ecu/core/trace/replay.hpp"
#include "ecu/core/trace/trace.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using namespace ecu::core;

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

class CountingEventSink final : public runtime::IEventSink {
 public:
  void publish(const runtime::EventView& event) noexcept override {
    ++count;
    last_type = event.header.type;
  }

  std::size_t count{0U};
  runtime::EventTypeId last_type{0U};
};

class CountingTraceSink final : public trace::ITraceSink {
 public:
  void record(const trace::TraceRecordView& record) noexcept override {
    ++count;
    last_category = record.header.category;
  }

  std::size_t count{0U};
  trace::TraceCategory last_category{trace::TraceCategory::core};
};

}  // namespace

int main() {
  int failures = 0;

  {
    runtime::LifecycleStateMachine lifecycle;

    failures += require(
        lifecycle.state() == runtime::LifecycleState::stopped,
        "lifecycle starts stopped");
    failures += require(
        lifecycle.mark_running() ==
            runtime::LifecycleStatus::invalid_transition,
        "cannot jump stopped -> running");
    failures += require(
        lifecycle.begin_start() == runtime::LifecycleStatus::ok &&
            lifecycle.state() == runtime::LifecycleState::starting,
        "begin start");
    failures += require(
        lifecycle.mark_running() == runtime::LifecycleStatus::ok &&
            lifecycle.state() == runtime::LifecycleState::running,
        "mark running");
    failures += require(
        lifecycle.begin_stop() == runtime::LifecycleStatus::ok &&
            lifecycle.mark_stopped() == runtime::LifecycleStatus::ok &&
            lifecycle.state() == runtime::LifecycleState::stopped,
        "normal stop sequence");

    lifecycle.mark_faulted();
    failures += require(
        lifecycle.state() == runtime::LifecycleState::faulted,
        "fault transition");
    failures += require(
        lifecycle.begin_start() ==
            runtime::LifecycleStatus::invalid_transition,
        "fault must be reset before start");
    failures += require(
        lifecycle.reset_fault() == runtime::LifecycleStatus::ok &&
            lifecycle.state() == runtime::LifecycleState::stopped,
        "fault reset");
  }

  {
    runtime::ResourceManager resources;
    const runtime::ResourceKey can0{
        runtime::ResourceClass::can_channel,
        0U};

    const auto first = resources.acquire(can0, 100U);
    failures += require(
        first.status == runtime::ResourceAcquireStatus::acquired &&
            first.lease.valid(),
        "first owner acquires CAN");

    const auto repeated = resources.acquire(can0, 100U);
    failures += require(
        repeated.status ==
            runtime::ResourceAcquireStatus::already_owned &&
            repeated.lease.generation == first.lease.generation,
        "same owner acquire is idempotent");

    const auto conflict = resources.acquire(can0, 200U);
    failures += require(
        conflict.status == runtime::ResourceAcquireStatus::busy,
        "second owner blocked");

    failures += require(
        resources.owns(can0, 100U) &&
            resources.current_owner(can0) == 100U,
        "ownership query");

    failures += require(
        resources.release(first.lease) ==
            runtime::ResourceReleaseStatus::released,
        "lease release");

    const auto second = resources.acquire(can0, 100U);
    failures += require(
        second.status == runtime::ResourceAcquireStatus::acquired &&
            second.lease.generation != first.lease.generation,
        "generation changes after reacquire");

    failures += require(
        resources.release(first.lease) ==
            runtime::ResourceReleaseStatus::stale_lease,
        "stale lease cannot release reacquired resource");

    failures += require(
        resources.release(second.lease) ==
            runtime::ResourceReleaseStatus::released &&
            resources.active_count() == 0U,
        "new lease releases resource");

    std::array<runtime::ResourceLease,
               runtime::ResourceManager::kMaxResources> leases{};

    for (std::size_t i = 0U; i < leases.size(); ++i) {
      const auto acquired = resources.acquire(
          runtime::ResourceKey{
              runtime::ResourceClass::custom,
              static_cast<std::uint32_t>(i)},
          static_cast<runtime::ResourceOwnerId>(i + 1U));
      failures += require(
          acquired.status == runtime::ResourceAcquireStatus::acquired,
          "resource table fill");
      leases[i] = acquired.lease;
    }

    failures += require(
        resources.acquire(
            runtime::ResourceKey{
                runtime::ResourceClass::custom,
                1000U},
            999U).status ==
            runtime::ResourceAcquireStatus::capacity_exhausted,
        "resource capacity gate");

    for (const auto& lease : leases) {
      static_cast<void>(resources.release(lease));
    }
  }

  {
    runtime::CancellationFlag cancellation;
    failures += require(
        !cancellation.requested(),
        "cancellation starts clear");
    cancellation.request();
    failures += require(
        cancellation.requested(),
        "cancellation request visible");
    cancellation.reset();
    failures += require(
        !cancellation.requested(),
        "cancellation reset");
  }

  {
    runtime::CommandHeader command{};
    failures += require(
        !runtime::is_valid_command_header(command),
        "zero command identifiers rejected");

    command.id = 1U;
    command.type = 10U;
    failures += require(
        runtime::is_valid_command_header(command),
        "command header accepted");

    failures += require(
        runtime::next_state_revision(41U) == 42U,
        "state revision increments");
    failures += require(
        runtime::next_state_revision(
            std::numeric_limits<runtime::StateRevision>::max()) ==
            std::numeric_limits<runtime::StateRevision>::max(),
        "state revision saturates");
  }

  {
    CountingEventSink events;
    const std::byte payload[]{std::byte{0x01}};

    runtime::EventView event{};
    event.header.sequence = 1U;
    event.header.type = 44U;
    event.payload = payload;
    event.payload_size = 1U;

    events.publish(event);
    failures += require(
        events.count == 1U && events.last_type == 44U,
        "event sink contract");

    CountingTraceSink traces;
    trace::TraceRecordView record{};
    record.header.category = trace::TraceCategory::uds;
    record.payload = payload;
    record.payload_size = 1U;

    traces.record(record);
    failures += require(
        traces.count == 1U &&
            traces.last_category == trace::TraceCategory::uds,
        "trace sink contract");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "CORE_FOUNDATION_TESTS=PASS\n";
  return 0;
}
