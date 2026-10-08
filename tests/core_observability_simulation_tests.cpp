#include "ecu/core/runtime/event_bus.hpp"
#include "ecu/core/simulation/simulated_can.hpp"
#include "ecu/core/trace/memory_trace_buffer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using namespace ecu::core;

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

class FakeClock final : public time::IMonotonicClock {
 public:
  time::MonotonicTime now() const noexcept override {
    return now_;
  }

  void advance(const std::chrono::milliseconds delta) noexcept {
    now_ += std::chrono::duration_cast<time::MonotonicTime>(delta);
  }

 private:
  time::MonotonicTime now_{0};
};

class CaptureSink final : public runtime::IEventSink {
 public:
  void publish(const runtime::EventView& event) noexcept override {
    ++count;
    last = event.header;
    payload_size = event.payload_size;
  }

  std::size_t count{0U};
  runtime::EventHeader last{};
  std::size_t payload_size{0U};
};

}  // namespace

int main() {
  int failures = 0;

  {
    FakeClock clock;
    runtime::EventBus bus{clock};
    CaptureSink all;
    CaptureSink typed;

    failures += require(
        bus.subscribe(runtime::EventBus::kAllEventTypes, all) ==
            runtime::EventSubscriptionStatus::subscribed,
        "subscribe all-events sink");

    failures += require(
        bus.subscribe(42U, typed) ==
            runtime::EventSubscriptionStatus::subscribed,
        "subscribe typed sink");

    const std::array<std::byte, 2> payload{
        std::byte{0xAA}, std::byte{0x55}};

    runtime::EventView event{};
    event.header.type = 42U;
    event.header.correlation_id = 99U;
    event.payload = payload.data();
    event.payload_size = payload.size();

    clock.advance(std::chrono::milliseconds{5});
    bus.publish(event);

    failures += require(
        all.count == 1U &&
            typed.count == 1U &&
            all.last.sequence == 1U &&
            typed.last.sequence == 1U &&
            all.last.timestamp == std::chrono::milliseconds{5} &&
            typed.payload_size == 2U,
        "event sequence and timestamp assigned");

    event.header.type = 43U;
    clock.advance(std::chrono::milliseconds{1});
    bus.publish(event);

    failures += require(
        all.count == 2U &&
            typed.count == 1U &&
            bus.last_sequence() == 2U,
        "event filtering");

    failures += require(
        bus.unsubscribe(42U, typed) &&
            bus.subscription_count() == 1U,
        "event unsubscribe");
  }

  {
    trace::MemoryTraceBuffer<3U, 4U> buffer;

    for (std::uint32_t i = 1U; i <= 4U; ++i) {
      const std::array<std::byte, 5> payload{
          static_cast<std::byte>(i),
          std::byte{0x02},
          std::byte{0x03},
          std::byte{0x04},
          std::byte{0x05}};

      trace::TraceRecordView record{};
      record.header.category = trace::TraceCategory::can;
      record.header.subject_id = i;
      record.payload = payload.data();
      record.payload_size = payload.size();
      buffer.record(record);
    }

    failures += require(
        buffer.size() == 3U &&
            buffer.last_sequence() == 4U,
        "flight recorder bounded ring");

    buffer.reset();

    std::array<std::byte, 8> replay_payload{};
    trace::TraceRecordHeader header{};
    std::size_t replay_size = 0U;

    failures += require(
        buffer.next(
            header,
            replay_payload.data(),
            replay_payload.size(),
            replay_size) == trace::ReplayStatus::ok &&
            header.sequence == 2U &&
            header.subject_id == 2U &&
            header.truncated &&
            replay_size == 4U &&
            replay_payload[0] == std::byte{0x02},
        "flight recorder oldest retained record");

    failures += require(
        buffer.next(
            header,
            replay_payload.data(),
            replay_payload.size(),
            replay_size) == trace::ReplayStatus::ok &&
            header.sequence == 3U,
        "flight recorder second record");

    failures += require(
        buffer.next(
            header,
            replay_payload.data(),
            replay_payload.size(),
            replay_size) == trace::ReplayStatus::ok &&
            header.sequence == 4U,
        "flight recorder newest record");

    failures += require(
        buffer.next(
            header,
            replay_payload.data(),
            replay_payload.size(),
            replay_size) == trace::ReplayStatus::end_of_stream,
        "flight recorder replay end");
  }

  {
    simulation::SimulatedCanInterface can;

    transport::CanChannelConfig config{};
    config.nominal_bitrate = 500000U;
    config.fd_enabled = true;
    config.data_bitrate = 2000000U;
    config.mode = transport::CanMode::normal;

    failures += require(
        can.open(config) == transport::CanStatus::ok &&
            can.is_open(),
        "simulated CAN opens");

    transport::CanFrame tx{};
    tx.identifier = 0x123U;
    tx.format = transport::CanFrameFormat::fd;
    tx.length = 12U;
    tx.bit_rate_switch = true;
    tx.payload[0] = std::byte{0x7A};

    failures += require(
        can.send(tx) == transport::CanStatus::ok &&
            can.pending_tx() == 1U,
        "simulated CAN TX queue");

    transport::CanFrame captured{};
    failures += require(
        can.take_tx(captured) &&
            captured.identifier == 0x123U &&
            captured.payload[0] == std::byte{0x7A},
        "simulated CAN TX capture");

    transport::ReceivedCanFrame rx{};
    rx.frame.identifier = 0x456U;
    rx.frame.length = 2U;
    rx.frame.payload[0] = std::byte{0x11};
    rx.timestamp = std::chrono::milliseconds{9};

    failures += require(
        can.inject_rx(rx) &&
            can.pending_rx() == 1U,
        "simulated CAN RX inject");

    const auto received = can.try_receive();
    failures += require(
        received.status == transport::CanStatus::ok &&
            received.value.frame.identifier == 0x456U &&
            received.value.timestamp == std::chrono::milliseconds{9},
        "simulated CAN receive");

    can.close();
    failures += require(
        can.send(tx) == transport::CanStatus::not_open,
        "simulated CAN closed guard");
  }

  {
    simulation::SimulatedCanInterface can;

    transport::CanChannelConfig config{};
    config.nominal_bitrate = 500000U;
    config.mode = transport::CanMode::listen_only;

    failures += require(
        can.open(config) == transport::CanStatus::ok,
        "simulated CAN listen-only opens");

    transport::CanFrame frame{};
    frame.identifier = 0x100U;

    failures += require(
        can.send(frame) == transport::CanStatus::unsupported,
        "simulated CAN listen-only TX guard");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "CORE_OBSERVABILITY_SIMULATION_TESTS=PASS\n";
  return 0;
}
