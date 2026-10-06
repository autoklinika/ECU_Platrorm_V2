#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core/protocol/uds/uds_client.hpp"
#include "ecu/sac/sac_identification.hpp"
#include "ecu/sac/sac_profile.hpp"
#include "ecu/sac/sac_runtime.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

IsoTpConfig classic_isotp() {
  IsoTpConfig config{};
  config.frame_format = CanFrameFormat::classic;
  config.tx_data_length = 8U;
  config.flow_control_timeout = std::chrono::milliseconds{50};
  config.consecutive_frame_timeout = std::chrono::milliseconds{50};
  return config;
}

std::vector<std::byte> make_positive_did(
    const std::uint16_t did,
    const std::string& value) {
  std::vector<std::byte> response;
  response.reserve(3U + value.size());
  response.push_back(std::byte{0x62});
  response.push_back(
      static_cast<std::byte>((did >> 8U) & 0xFFU));
  response.push_back(
      static_cast<std::byte>(did & 0xFFU));

  for (const char ch : value) {
    response.push_back(
        static_cast<std::byte>(
            static_cast<unsigned char>(ch)));
  }

  return response;
}

bool serve_identification(
    SacIdentification& identification,
    IsoTpEndpoint& server,
    FakeClock& clock) {
  for (std::size_t i = 0U; i < 5000U; ++i) {
    const auto status = identification.poll();
    const auto server_status = server.poll();

    if (status == SacIdentificationStatus::done) {
      return true;
    }

    if (status == SacIdentificationStatus::uds_error ||
        status == SacIdentificationStatus::invalid_response) {
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
          request.length != 3U ||
          std::to_integer<std::uint8_t>(
              request.payload[0]) != 0x22U) {
        return false;
      }

      const auto did =
          static_cast<std::uint16_t>(
              (static_cast<std::uint16_t>(
                   std::to_integer<std::uint8_t>(
                       request.payload[1]))
               << 8U) |
              std::to_integer<std::uint8_t>(
                  request.payload[2]));

      std::vector<std::byte> response;
      if (did == kDidVin) {
        response = make_positive_did(
            did,
            "XLRTE47MS0E123456");
      } else if (did == kDidSoftware) {
        response = make_positive_did(
            did,
            "SAC-SW-01.02.03");
      } else if (did == kDidHardware) {
        response = make_positive_did(
            did,
            "SAC-HW-A");
      } else {
        return false;
      }

      if (server.start_send(
              response.data(),
              response.size()) !=
          IsoTpStatus::in_progress) {
        return false;
      }
    }

    clock.advance(std::chrono::milliseconds{1});
  }

  return false;
}

CanFrame make_pressure_frame() {
  CanFrame frame{};
  frame.identifier_format =
      CanIdentifierFormat::extended_29_bit;
  frame.format = CanFrameFormat::classic;
  frame.type = CanFrameType::data;
  frame.length = 8U;

  const std::uint32_t pgn = kPgnPressures;
  const std::uint32_t pf = (pgn >> 8U) & 0xFFU;
  const std::uint32_t ps = pgn & 0xFFU;
  const std::uint32_t source_address = 0x30U;

  frame.identifier =
      (6U << 26U) |
      (pf << 16U) |
      (ps << 8U) |
      source_address;

  frame.payload[2] = std::byte{100};
  frame.payload[3] = std::byte{50};
  return frame;
}

}  // namespace

int main() {
  int failures = 0;

  {
    constexpr auto address = diagnostic_address();
    failures += require(
        address.tx_id == 0x18DA30F9U,
        "SAC request ID");
    failures += require(
        address.rx_id == 0x18DAF930U,
        "SAC response ID");
    failures += require(
        address.identifier_format ==
            CanIdentifierFormat::extended_29_bit,
        "SAC uses 29-bit addressing");

    failures += require(
        kLegacyBitrateCandidates[0] == 250000U &&
            kLegacyBitrateCandidates[1] == 500000U,
        "legacy bitrate candidates preserved");
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

    const auto isotp_config = classic_isotp();

    IsoTpEndpoint client_tp{
        client_can,
        clock,
        address,
        isotp_config};

    IsoTpEndpoint server_tp{
        server_can,
        clock,
        reverse,
        isotp_config};

    UdsClient uds{
        client_tp,
        clock,
        UdsTiming{
            std::chrono::milliseconds{100},
            std::chrono::milliseconds{5000}}};

    SacIdentification identification{uds};

    failures += require(
        identification.start() ==
            SacIdentificationStatus::in_progress,
        "SAC identification starts");

    failures += require(
        serve_identification(
            identification,
            server_tp,
            clock),
        "SAC identification completes");

    const auto& result = identification.result();

    failures += require(
        result.vin.view() == "XLRTE47MS0E123456",
        "VIN parsed");
    failures += require(
        result.software.view() == "SAC-SW-01.02.03",
        "software ID parsed");
    failures += require(
        result.hardware.view() == "SAC-HW-A",
        "hardware ID parsed");
  }

  {
    auto frame = make_pressure_frame();
    SacPressureState pressure{};

    failures += require(
        decode_j1939_pgn(frame.identifier) ==
            kPgnPressures,
        "SAC pressure PGN decoded");

    failures += require(
        decode_pressure_broadcast(
            frame,
            pressure),
        "pressure broadcast accepted");

    failures += require(
        pressure.valid &&
            pressure.pressure1_bar > 7.99F &&
            pressure.pressure1_bar < 8.01F &&
            pressure.pressure2_bar > 3.99F &&
            pressure.pressure2_bar < 4.01F,
        "pressure scaling matches legacy evidence");

    frame.identifier = 0x18DAF930U;
    failures += require(
        !decode_pressure_broadcast(
            frame,
            pressure),
        "UDS PF 0xDA excluded from pressure decoder");
  }

  {
    UdsResponse response{};
    response.status = UdsStatus::ok;
    response.request_sid = 0x22U;
    response.response_sid = 0x62U;
    response.length = 11U;
    response.payload[0] = std::byte{0x62};
    response.payload[1] = std::byte{0xFE};
    response.payload[2] = std::byte{0x96};

    response.payload[7] = std::byte{0x00};
    response.payload[8] = std::byte{0xF0};
    response.payload[9] = std::byte{0x00};
    response.payload[10] = std::byte{0xE6};

    SacVoltageState voltage{};

    failures += require(
        decode_voltage_did_response(
            response,
            voltage),
        "voltage DID response accepted");

    failures += require(
        voltage.valid &&
            voltage.permanent_v > 23.99F &&
            voltage.permanent_v < 24.01F &&
            voltage.ignition_v > 22.99F &&
            voltage.ignition_v < 23.01F,
        "voltage scaling matches legacy evidence");
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

    const auto isotp_config = classic_isotp();

    IsoTpEndpoint client_tp{
        client_can,
        clock,
        address,
        isotp_config};

    IsoTpEndpoint server_tp{
        server_can,
        clock,
        reverse,
        isotp_config};

    UdsClient uds{
        client_tp,
        clock,
        UdsTiming{
            std::chrono::milliseconds{100},
            std::chrono::milliseconds{5000}}};

    SacIdentification identification{uds};
    static_cast<void>(identification.start());

    for (std::size_t i = 0U; i < 1000U; ++i) {
      static_cast<void>(identification.poll());
      static_cast<void>(server_tp.poll());

      if (server_tp.has_received()) {
        static_cast<void>(server_tp.take_received());
        const std::byte negative[] = {
            std::byte{0x7F},
            std::byte{0x22},
            std::byte{0x31}};
        static_cast<void>(
            server_tp.start_send(
                negative,
                3U));
      }

      if (identification.status() ==
          SacIdentificationStatus::uds_error) {
        break;
      }

      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        identification.status() ==
            SacIdentificationStatus::uds_error &&
            identification.last_nrc() == 0x31U,
        "SAC identification preserves NRC");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "SAC_MODULE_TESTS=PASS\n";
  return 0;
}
