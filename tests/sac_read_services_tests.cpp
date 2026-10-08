#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core/protocol/isotp/isotp_diagnostic_transport.hpp"
#include "ecu/core/protocol/uds/uds_client.hpp"
#include "ecu/core/protocol/uds/uds_services.hpp"
#include "ecu/sac/sac_dtc_reader.hpp"
#include "ecu/sac/sac_profile.hpp"
#include "ecu/sac/sac_runtime_monitor.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <vector>

namespace {

using namespace ecu::core;
using namespace ecu::core::protocol::isotp;
using namespace ecu::core::protocol::uds;
using namespace ecu::core::transport;
using namespace ecu::sac;

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

class FakeCan final : public ICanInterface {
 public:
  explicit FakeCan(FakeClock& clock) : clock_(clock) {}

  void connect(FakeCan& peer) noexcept {
    peer_ = &peer;
  }

  CanCapabilities capabilities() const noexcept override {
    return CanCapabilities{true, true, true, true, 64U};
  }

  CanStatus open(const CanChannelConfig&) noexcept override {
    open_ = true;
    return CanStatus::ok;
  }

  void close() noexcept override {
    open_ = false;
  }

  bool is_open() const noexcept override {
    return open_;
  }

  CanStatus send(const CanFrame& frame) noexcept override {
    if (!open_) {
      return CanStatus::not_open;
    }

    if (peer_ != nullptr) {
      peer_->rx_.push_back(
          ReceivedCanFrame{frame, clock_.now()});
    }

    return CanStatus::ok;
  }

  CanReceiveResult try_receive() noexcept override {
    if (rx_.empty()) {
      return CanReceiveResult{CanStatus::would_block, {}};
    }

    const auto value = rx_.front();
    rx_.pop_front();
    return CanReceiveResult{CanStatus::ok, value};
  }

 private:
  FakeClock& clock_;
  FakeCan* peer_{nullptr};
  bool open_{true};
  std::deque<ReceivedCanFrame> rx_{};
};

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

IsoTpConfig classic_isotp() {
  IsoTpConfig config{};
  config.frame_format = CanFrameFormat::classic;
  config.tx_data_length = 8U;
  config.flow_control_timeout = std::chrono::milliseconds{50};
  config.consecutive_frame_timeout = std::chrono::milliseconds{50};
  return config;
}

CanFrame pressure_frame() {
  CanFrame frame{};
  frame.identifier_format =
      CanIdentifierFormat::extended_29_bit;
  frame.format = CanFrameFormat::classic;
  frame.type = CanFrameType::data;
  frame.length = 8U;

  const auto pgn = kPgnPressures;
  const auto pf = (pgn >> 8U) & 0xFFU;
  const auto ps = pgn & 0xFFU;

  frame.identifier =
      (6U << 26U) |
      (pf << 16U) |
      (ps << 8U) |
      0x30U;

  frame.payload[2] = std::byte{125};
  frame.payload[3] = std::byte{25};
  return frame;
}

std::uint16_t did_from_request(
    const IsoTpReceiveResult& request) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(
           std::to_integer<std::uint8_t>(
               request.payload[1]))
       << 8U) |
      std::to_integer<std::uint8_t>(
          request.payload[2]));
}

}  // namespace

int main() {
  int failures = 0;

  {
    const auto request =
        make_read_dtc_information_by_status_mask(0xAAU);

    failures += require(
        request.length == 3U &&
            std::to_integer<std::uint8_t>(
                request.payload[0]) == 0x19U &&
            std::to_integer<std::uint8_t>(
                request.payload[1]) == 0x02U &&
            std::to_integer<std::uint8_t>(
                request.payload[2]) == 0xAAU,
        "ReadDTCInformation builder");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    const auto address = diagnostic_address();
    const IsoTpAddress reverse{
        address.rx_id,
        address.tx_id,
        address.identifier_format};
    const auto iso = classic_isotp();

    IsoTpEndpoint client_tp{
        client_can, clock, address, iso};
    IsoTpEndpoint server_tp{
        server_can, clock, reverse, iso};

    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient uds{
        diagnostic_transport,
        clock,
        UdsTiming{
            std::chrono::milliseconds{100},
            std::chrono::milliseconds{5000}}};

    SacDtcReader reader{uds};
    failures += require(
        reader.start(0xFFU) ==
            SacDtcReadStatus::in_progress,
        "DTC reader starts");

    bool session_seen = false;
    bool dtc_seen = false;

    for (std::size_t i = 0U; i < 5000U; ++i) {
      const auto reader_status = reader.poll();
      const auto server_status = server_tp.poll();

      if (reader_status == SacDtcReadStatus::done) {
        break;
      }

      if (reader_status == SacDtcReadStatus::uds_error ||
          reader_status == SacDtcReadStatus::invalid_response ||
          reader_status == SacDtcReadStatus::capacity_exceeded) {
        break;
      }

      if (server_status != IsoTpStatus::ok &&
          server_status != IsoTpStatus::idle &&
          server_status != IsoTpStatus::in_progress &&
          server_status != IsoTpStatus::would_block) {
        break;
      }

      if (server_tp.has_received()) {
        const auto request = server_tp.take_received();
        const auto sid =
            std::to_integer<std::uint8_t>(
                request.payload[0]);

        if (sid == 0x10U) {
          session_seen = true;
          const std::byte response[] = {
              std::byte{0x50},
              std::byte{0x03},
              std::byte{0x00},
              std::byte{0x32},
              std::byte{0x01},
              std::byte{0xF4}};
          static_cast<void>(
              server_tp.start_send(response, 6U));
        } else if (sid == 0x19U) {
          dtc_seen = true;
          const std::byte response[] = {
              std::byte{0x59},
              std::byte{0x02},
              std::byte{0xFF},
              std::byte{0x12},
              std::byte{0x34},
              std::byte{0x56},
              std::byte{0xA5},
              std::byte{0x00},
              std::byte{0x01},
              std::byte{0x02},
              std::byte{0x08}};
          static_cast<void>(
              server_tp.start_send(response, 11U));
        }
      }

      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        session_seen && dtc_seen,
        "DTC flow uses session then service 0x19");
    failures += require(
        reader.status() == SacDtcReadStatus::done,
        "DTC reader completes");

    const auto& result = reader.result();
    failures += require(
        result.status_availability_mask == 0xFFU &&
            result.count == 2U,
        "DTC response header parsed");
    failures += require(
        result.records[0].code == 0x123456U &&
            result.records[0].status == 0xA5U,
        "first DTC parsed");
    failures += require(
        result.records[1].code == 0x000102U &&
            result.records[1].status == 0x08U,
        "second DTC parsed");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    const auto address = diagnostic_address();
    const IsoTpAddress reverse{
        address.rx_id,
        address.tx_id,
        address.identifier_format};
    const auto iso = classic_isotp();

    IsoTpEndpoint client_tp{
        client_can, clock, address, iso};
    IsoTpEndpoint server_tp{
        server_can, clock, reverse, iso};

    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient uds{
        diagnostic_transport,
        clock,
        UdsTiming{
            std::chrono::milliseconds{100},
            std::chrono::milliseconds{5000}}};

    SacDtcReader reader{uds};
    static_cast<void>(reader.start());

    bool session_completed = false;

    for (std::size_t i = 0U; i < 3000U; ++i) {
      static_cast<void>(reader.poll());
      static_cast<void>(server_tp.poll());

      if (server_tp.has_received()) {
        const auto request = server_tp.take_received();
        const auto sid =
            std::to_integer<std::uint8_t>(
                request.payload[0]);

        if (sid == 0x10U) {
          session_completed = true;
          const std::byte response[] = {
              std::byte{0x50},
              std::byte{0x03}};
          static_cast<void>(
              server_tp.start_send(response, 2U));
        } else if (sid == 0x19U) {
          const std::byte response[] = {
              std::byte{0x7F},
              std::byte{0x19},
              std::byte{0x31}};
          static_cast<void>(
              server_tp.start_send(response, 3U));
        }
      }

      if (reader.status() == SacDtcReadStatus::uds_error) {
        break;
      }

      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        session_completed &&
            reader.status() == SacDtcReadStatus::uds_error &&
            reader.last_nrc() == 0x31U,
        "DTC reader preserves NRC");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    const auto address = diagnostic_address();
    const IsoTpAddress reverse{
        address.rx_id,
        address.tx_id,
        address.identifier_format};
    const auto iso = classic_isotp();

    IsoTpEndpoint client_tp{
        client_can, clock, address, iso};
    IsoTpEndpoint server_tp{
        server_can, clock, reverse, iso};

    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient uds{
        diagnostic_transport,
        clock,
        UdsTiming{
            std::chrono::milliseconds{100},
            std::chrono::milliseconds{5000}}};

    SacRuntimeMonitor runtime{uds};

    const auto frame = pressure_frame();
    failures += require(
        runtime.ingest_can_frame(frame),
        "runtime accepts pressure broadcast");
    failures += require(
        runtime.pressure().valid &&
            runtime.pressure().pressure1_bar > 9.99F &&
            runtime.pressure().pressure1_bar < 10.01F &&
            runtime.pressure().pressure2_bar > 1.99F &&
            runtime.pressure().pressure2_bar < 2.01F,
        "runtime pressure state updated");

    failures += require(
        runtime.start_voltage_read() ==
            SacRuntimeStatus::in_progress,
        "voltage read starts");

    failures += require(
        runtime.start_voltage_read() ==
            SacRuntimeStatus::busy,
        "parallel voltage read blocked");

    bool request_seen = false;
    for (std::size_t i = 0U; i < 3000U; ++i) {
      const auto runtime_status = runtime.poll();
      const auto server_status = server_tp.poll();

      if (runtime_status == SacRuntimeStatus::voltage_updated) {
        break;
      }

      if (runtime_status == SacRuntimeStatus::uds_error ||
          runtime_status == SacRuntimeStatus::invalid_response) {
        break;
      }

      if (server_status != IsoTpStatus::ok &&
          server_status != IsoTpStatus::idle &&
          server_status != IsoTpStatus::in_progress &&
          server_status != IsoTpStatus::would_block) {
        break;
      }

      if (server_tp.has_received()) {
        const auto request = server_tp.take_received();

        if (request.length == 3U &&
            std::to_integer<std::uint8_t>(
                request.payload[0]) == 0x22U &&
            did_from_request(request) == kDidVoltage) {
          request_seen = true;

          const std::byte response[] = {
              std::byte{0x62},
              std::byte{0xFE},
              std::byte{0x96},
              std::byte{0x00},
              std::byte{0x00},
              std::byte{0x00},
              std::byte{0x00},
              std::byte{0x00},
              std::byte{0xF0},
              std::byte{0x00},
              std::byte{0xE6}};

          static_cast<void>(
              server_tp.start_send(response, 11U));
        }
      }

      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        request_seen,
        "runtime requests DID FE96");
    failures += require(
        runtime.status() ==
            SacRuntimeStatus::voltage_updated,
        "runtime voltage read completes");
    failures += require(
        runtime.voltage().valid &&
            runtime.voltage().permanent_v > 23.99F &&
            runtime.voltage().permanent_v < 24.01F &&
            runtime.voltage().ignition_v > 22.99F &&
            runtime.voltage().ignition_v < 23.01F,
        "runtime voltage state updated");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "SAC_READ_SERVICES_TESTS=PASS\n";
  return 0;
}
