#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core/protocol/isotp/isotp_diagnostic_transport.hpp"
#include "ecu/core/protocol/uds/uds_client.hpp"
#include "ecu/sac/sac_controller.hpp"
#include "ecu/sac/sac_profile.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
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

IsoTpConfig iso_config() {
  IsoTpConfig config{};
  config.frame_format = CanFrameFormat::classic;
  config.tx_data_length = 8U;
  config.flow_control_timeout = std::chrono::milliseconds{50};
  config.consecutive_frame_timeout = std::chrono::milliseconds{50};
  return config;
}

std::uint16_t request_did(const IsoTpReceiveResult& request) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(
           std::to_integer<std::uint8_t>(request.payload[1]))
       << 8U) |
      std::to_integer<std::uint8_t>(request.payload[2]));
}

std::vector<std::byte> text_did_response(
    const std::uint16_t did,
    const std::string& text) {
  std::vector<std::byte> response;
  response.push_back(std::byte{0x62});
  response.push_back(
      static_cast<std::byte>((did >> 8U) & 0xFFU));
  response.push_back(
      static_cast<std::byte>(did & 0xFFU));

  for (const auto ch : text) {
    response.push_back(
        static_cast<std::byte>(
            static_cast<unsigned char>(ch)));
  }

  return response;
}

CanFrame pressure_frame() {
  CanFrame frame{};
  frame.identifier_format =
      CanIdentifierFormat::extended_29_bit;
  frame.format = CanFrameFormat::classic;
  frame.type = CanFrameType::data;
  frame.length = 8U;

  const auto pgn = kPgnPressures;
  frame.identifier =
      (6U << 26U) |
      (((pgn >> 8U) & 0xFFU) << 16U) |
      ((pgn & 0xFFU) << 8U) |
      0x30U;

  frame.payload[2] = std::byte{100};
  frame.payload[3] = std::byte{75};
  return frame;
}

bool service_server(
    IsoTpEndpoint& server,
    FakeClock& clock,
    SacController& controller,
    const SacControllerStatus wanted_status,
    const std::size_t limit = 5000U) {
  for (std::size_t i = 0U; i < limit; ++i) {
    static_cast<void>(controller.poll());
    const auto server_status = server.poll();

    if (controller.status() == wanted_status) {
      return true;
    }

    if (controller.status() == SacControllerStatus::error) {
      return false;
    }

    if (server_status != IsoTpStatus::ok &&
        server_status != IsoTpStatus::idle &&
        server_status != IsoTpStatus::in_progress &&
        server_status != IsoTpStatus::would_block) {
      return false;
    }

    if (server.has_received()) {
      const auto request = server.take_received();
      if (request.status != IsoTpStatus::ok ||
          request.length == 0U) {
        return false;
      }

      const auto sid =
          std::to_integer<std::uint8_t>(
              request.payload[0]);

      if (sid == 0x22U) {
        const auto did = request_did(request);
        std::vector<std::byte> response;

        if (did == kDidVin) {
          response = text_did_response(
              did,
              "XLRTE47MS0E123456");
        } else if (did == kDidSoftware) {
          response = text_did_response(
              did,
              "SAC-SW-01");
        } else if (did == kDidHardware) {
          response = text_did_response(
              did,
              "SAC-HW-A");
        } else if (did == kDidVoltage) {
          response = {
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
        } else {
          return false;
        }

        static_cast<void>(
            server.start_send(
                response.data(),
                response.size()));
      } else if (sid == 0x10U) {
        const std::byte response[] = {
            std::byte{0x50},
            std::byte{0x03},
            std::byte{0x00},
            std::byte{0x32},
            std::byte{0x01},
            std::byte{0xF4}};
        static_cast<void>(
            server.start_send(response, 6U));
      } else if (sid == 0x19U) {
        const std::byte response[] = {
            std::byte{0x59},
            std::byte{0x02},
            std::byte{0xFF},
            std::byte{0x12},
            std::byte{0x34},
            std::byte{0x56},
            std::byte{0x08}};
        static_cast<void>(
            server.start_send(response, 7U));
      } else {
        return false;
      }
    }

    clock.advance(std::chrono::milliseconds{1});
  }

  return false;
}

}  // namespace

int main() {
  int failures = 0;

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

  const auto iso = iso_config();

  IsoTpEndpoint client_tp{
      client_can,
      clock,
      address,
      iso};

  IsoTpEndpoint server_tp{
      server_can,
      clock,
      reverse,
      iso};

  IsoTpDiagnosticTransport diagnostic_transport{client_tp};
  UdsClient uds{
      diagnostic_transport,
      clock,
      UdsTiming{
          std::chrono::milliseconds{100},
          std::chrono::milliseconds{5000}}};

  SacController controller{uds};

  failures += require(
      controller.status() == SacControllerStatus::idle &&
          !controller.busy(),
      "controller starts idle");

  failures += require(
      controller.start_identification() ==
          SacControllerStatus::in_progress,
      "identification starts");

  failures += require(
      controller.start_dtc_read() ==
          SacControllerStatus::busy,
      "DTC blocked while identification active");

  const auto pressure = pressure_frame();
  failures += require(
      controller.ingest_can_frame(pressure),
      "pressure broadcast accepted during active UDS operation");

  failures += require(
      service_server(
          server_tp,
          clock,
          controller,
          SacControllerStatus::identification_ready),
      "identification completes through controller");

  failures += require(
      controller.identification().vin.view() ==
          "XLRTE47MS0E123456",
      "controller exposes VIN");

  failures += require(
      controller.pressure().valid &&
          controller.pressure().pressure1_bar > 7.99F &&
          controller.pressure().pressure1_bar < 8.01F &&
          controller.pressure().pressure2_bar > 5.99F &&
          controller.pressure().pressure2_bar < 6.01F,
      "controller preserves passive pressure state");

  failures += require(
      controller.start_voltage_read() ==
          SacControllerStatus::in_progress,
      "voltage read starts");

  failures += require(
      controller.start_identification() ==
          SacControllerStatus::busy,
      "identification blocked while voltage read active");

  failures += require(
      service_server(
          server_tp,
          clock,
          controller,
          SacControllerStatus::voltage_ready),
      "voltage read completes through controller");

  failures += require(
      controller.voltage().valid &&
          controller.voltage().permanent_v > 23.99F &&
          controller.voltage().permanent_v < 24.01F,
      "controller exposes voltage state");

  failures += require(
      controller.start_dtc_read(0xFFU) ==
          SacControllerStatus::in_progress,
      "DTC read starts after voltage completion");

  failures += require(
      service_server(
          server_tp,
          clock,
          controller,
          SacControllerStatus::dtc_ready),
      "DTC read completes through controller");

  failures += require(
      controller.dtcs().count == 1U &&
          controller.dtcs().records[0].code == 0x123456U &&
          controller.dtcs().records[0].status == 0x08U,
      "controller exposes DTC result");

  failures += require(
      !controller.busy() &&
          controller.operation() == SacOperation::idle,
      "controller returns to idle operation after completion");

  controller.reset();

  failures += require(
      controller.status() == SacControllerStatus::idle &&
          !controller.pressure().valid &&
          !controller.voltage().valid &&
          controller.identification().vin.length == 0U &&
          controller.dtcs().count == 0U,
      "controller reset clears product state");

  if (failures != 0) {
    return 1;
  }

  std::cout << "SAC_CONTROLLER_TESTS=PASS\n";
  return 0;
}
