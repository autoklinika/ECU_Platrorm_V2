#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core/protocol/isotp/isotp_diagnostic_transport.hpp"
#include "ecu/core/protocol/uds/uds_client.hpp"
#include "ecu/core/protocol/uds/uds_services.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

namespace {

using namespace ecu::core;
using namespace ecu::core::protocol::isotp;
using namespace ecu::core::protocol::uds;
using namespace ecu::core::transport;

class FakeClock final : public time::IMonotonicClock {
 public:
  time::MonotonicTime now() const noexcept override {
    return now_;
  }

  template <typename Rep, typename Period>
  void advance(const std::chrono::duration<Rep, Period> delta) noexcept {
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

IsoTpAddress client_address() {
  return IsoTpAddress{
      0x700U,
      0x708U,
      CanIdentifierFormat::standard_11_bit};
}

IsoTpAddress server_address() {
  return IsoTpAddress{
      0x708U,
      0x700U,
      CanIdentifierFormat::standard_11_bit};
}

IsoTpConfig isotp_config() {
  IsoTpConfig config{};
  config.frame_format = CanFrameFormat::classic;
  config.tx_data_length = 8U;
  config.flow_control_timeout = std::chrono::milliseconds{20};
  config.consecutive_frame_timeout = std::chrono::milliseconds{20};
  return config;
}

UdsTiming uds_timing() {
  return UdsTiming{
      std::chrono::milliseconds{5},
      std::chrono::milliseconds{50}};
}

std::vector<std::byte> to_vector(
    const UdsRequest& request) {
  return std::vector<std::byte>(
      request.payload.begin(),
      request.payload.begin() +
          static_cast<std::ptrdiff_t>(request.length));
}

bool same_payload(
    const IsoTpReceiveResult& result,
    const std::vector<std::byte>& expected) {
  return result.status == IsoTpStatus::ok &&
         result.length == expected.size() &&
         std::memcmp(
             result.payload.data(),
             expected.data(),
             expected.size()) == 0;
}

bool send_server_payload(
    IsoTpEndpoint& server,
    const std::vector<std::byte>& payload) {
  return server.start_send(
             payload.data(),
             payload.size()) ==
         IsoTpStatus::in_progress;
}

bool pump_server_response(
    UdsClient& client,
    IsoTpEndpoint& server,
    FakeClock& clock,
    const std::vector<std::byte>& expected_request,
    const std::vector<std::byte>& response,
    const std::size_t max_iterations = 2000U) {
  bool response_started = false;

  for (std::size_t i = 0U; i < max_iterations; ++i) {
    const auto client_status = client.poll();
    const auto server_status = server.poll();

    if (client.has_response()) {
      return true;
    }

    const auto fatal_client =
        client_status != UdsStatus::ok &&
        client_status != UdsStatus::idle &&
        client_status != UdsStatus::in_progress &&
        client_status != UdsStatus::negative_response;

    const auto fatal_server =
        server_status != IsoTpStatus::ok &&
        server_status != IsoTpStatus::idle &&
        server_status != IsoTpStatus::in_progress &&
        server_status != IsoTpStatus::would_block;

    if (fatal_client || fatal_server) {
      return false;
    }

    if (!response_started && server.has_received()) {
      const auto request = server.take_received();
      if (!same_payload(request, expected_request)) {
        return false;
      }

      if (!send_server_payload(server, response)) {
        return false;
      }
      response_started = true;
    }

    if (client.has_response()) {
      return true;
    }

    clock.advance(std::chrono::microseconds{100});
  }

  return false;
}

}  // namespace

int main() {
  int failures = 0;

  {
    const auto session =
        make_diagnostic_session_control(0x03U);
    failures += require(
        session.length == 2U &&
            std::to_integer<std::uint8_t>(
                session.payload[0]) == 0x10U &&
            std::to_integer<std::uint8_t>(
                session.payload[1]) == 0x03U,
        "DiagnosticSessionControl builder");

    failures += require(
        make_diagnostic_session_control(0U).length == 0U,
        "invalid session rejected");

    const auto did =
        make_read_data_by_identifier(0xF190U);
    failures += require(
        did.length == 3U &&
            std::to_integer<std::uint8_t>(
                did.payload[0]) == 0x22U &&
            std::to_integer<std::uint8_t>(
                did.payload[1]) == 0xF1U &&
            std::to_integer<std::uint8_t>(
                did.payload[2]) == 0x90U,
        "ReadDataByIdentifier builder");

    const auto tester = make_tester_present();
    failures += require(
        tester.length == 2U &&
            std::to_integer<std::uint8_t>(
                tester.payload[0]) == 0x3EU &&
            std::to_integer<std::uint8_t>(
                tester.payload[1]) == 0x00U,
        "TesterPresent builder");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport, clock, uds_timing()};

    const auto request =
        make_read_data_by_identifier(0xF190U);
    const std::vector<std::byte> response{
        std::byte{0x62},
        std::byte{0xF1},
        std::byte{0x90},
        std::byte{'S'},
        std::byte{'A'},
        std::byte{'C'}};

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "positive request starts");

    failures += require(
        pump_server_response(
            client,
            server_tp,
            clock,
            to_vector(request),
            response),
        "positive response completes");

    const auto result = client.take_response();
    failures += require(
        result.status == UdsStatus::ok &&
            result.request_sid == 0x22U &&
            result.response_sid == 0x62U &&
            result.length == response.size() &&
            std::memcmp(
                result.payload.data(),
                response.data(),
                response.size()) == 0,
        "positive response validated and preserved");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport, clock, uds_timing()};

    const auto request =
        make_read_data_by_identifier(0x1234U);
    const std::vector<std::byte> response{
        std::byte{0x7F},
        std::byte{0x22},
        std::byte{0x31}};

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "negative request starts");

    failures += require(
        pump_server_response(
            client,
            server_tp,
            clock,
            to_vector(request),
            response),
        "negative response completes");

    const auto result = client.take_response();
    failures += require(
        result.status == UdsStatus::negative_response &&
            result.negative_response_code == 0x31U,
        "NRC preserved");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport, clock, uds_timing()};

    const auto request =
        make_read_data_by_identifier(0xF190U);

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "pending request starts");

    bool request_seen = false;
    bool pending_sent = false;
    bool final_sent = false;

    for (std::size_t i = 0U; i < 1000U; ++i) {
      const auto client_status = client.poll();
      const auto server_status = server_tp.poll();

      if (!request_seen && server_tp.has_received()) {
        const auto rx = server_tp.take_received();
        request_seen = same_payload(rx, to_vector(request));
      }

      if (request_seen && !pending_sent) {
        pending_sent = send_server_payload(
            server_tp,
            {
                std::byte{0x7F},
                std::byte{0x22},
                std::byte{0x78}});
      } else if (
          pending_sent &&
          !final_sent &&
          clock.now() >= std::chrono::milliseconds{10} &&
          !server_tp.tx_busy()) {
        final_sent = send_server_payload(
            server_tp,
            {
                std::byte{0x62},
                std::byte{0xF1},
                std::byte{0x90},
                std::byte{0x01}});
      }

      if (client.has_response()) {
        break;
      }

      const bool fatal_client =
          client_status == UdsStatus::timeout_p2 ||
          client_status == UdsStatus::timeout_p2_star ||
          client_status == UdsStatus::transport_error ||
          client_status == UdsStatus::protocol_error;
      const bool fatal_server =
          server_status != IsoTpStatus::ok &&
          server_status != IsoTpStatus::idle &&
          server_status != IsoTpStatus::in_progress &&
          server_status != IsoTpStatus::would_block;

      if (fatal_client || fatal_server) {
        break;
      }

      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        client.has_response(),
        "ResponsePending extends wait to final response");

    const auto result = client.take_response();
    failures += require(
        result.status == UdsStatus::ok &&
            result.response_sid == 0x62U,
        "final response after NRC 0x78 accepted");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport, clock, uds_timing()};

    const auto request =
        make_read_data_by_identifier(0xF190U);

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "P2 timeout request starts");
    failures += require(
        client.poll() == UdsStatus::in_progress,
        "P2 wait begins");

    clock.advance(std::chrono::milliseconds{5});
    failures += require(
        client.poll() == UdsStatus::timeout_p2,
        "P2 timeout detected");
    failures += require(
        client.take_response().status ==
            UdsStatus::timeout_p2,
        "P2 timeout result preserved");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport,
        clock,
        UdsTiming{
            std::chrono::milliseconds{5},
            std::chrono::milliseconds{20}}};

    const auto request =
        make_read_data_by_identifier(0xF190U);
    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "P2* timeout request starts");

    bool pending_delivered = false;
    for (std::size_t i = 0U; i < 100U; ++i) {
      static_cast<void>(client.poll());
      static_cast<void>(server_tp.poll());

      if (server_tp.has_received()) {
        static_cast<void>(server_tp.take_received());
        static_cast<void>(send_server_payload(
            server_tp,
            {
                std::byte{0x7F},
                std::byte{0x22},
                std::byte{0x78}}));
      }

      if (!server_tp.tx_busy()) {
        const auto status = client.poll();
        if (status == UdsStatus::in_progress &&
            clock.now() > std::chrono::milliseconds{1}) {
          pending_delivered = true;
          break;
        }
      }
      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        pending_delivered,
        "NRC 0x78 delivered before P2* timeout test");

    clock.advance(std::chrono::milliseconds{20});
    failures += require(
        client.poll() == UdsStatus::timeout_p2_star,
        "P2* timeout detected");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport,
        clock,
        UdsTiming{
            std::chrono::milliseconds{10},
            std::chrono::milliseconds{100}}};

    const auto request =
        make_diagnostic_session_control(0x03U);
    const std::vector<std::byte> response{
        std::byte{0x50},
        std::byte{0x03},
        std::byte{0x00},
        std::byte{0x32},
        std::byte{0x01},
        std::byte{0xF4}};

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "session request starts");

    failures += require(
        pump_server_response(
            client,
            server_tp,
            clock,
            to_vector(request),
            response),
        "session response completes");

    const auto result = client.take_response();
    failures += require(
        result.status == UdsStatus::ok,
        "session response positive");

    const auto timing = client.timing();
    failures += require(
        timing.p2 == std::chrono::milliseconds{50} &&
            timing.p2_star == std::chrono::milliseconds{5000},
        "session response updates P2/P2* timings");

    UdsTiming parsed{};
    failures += require(
        parse_session_control_timing(result, parsed) &&
            parsed.p2 == std::chrono::milliseconds{50} &&
            parsed.p2_star ==
                std::chrono::milliseconds{5000},
        "session timing parser");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport, clock, uds_timing()};

    const auto request =
        make_read_data_by_identifier(0xF190U);

    std::vector<std::byte> response(100U);
    response[0] = std::byte{0x62};
    response[1] = std::byte{0xF1};
    response[2] = std::byte{0x90};
    for (std::size_t i = 3U; i < response.size(); ++i) {
      response[i] =
          static_cast<std::byte>(
              static_cast<std::uint8_t>(i));
    }

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "long response request starts");

    failures += require(
        pump_server_response(
            client,
            server_tp,
            clock,
            to_vector(request),
            response,
            5000U),
        "multi-frame UDS response completes");

    const auto result = client.take_response();
    failures += require(
        result.status == UdsStatus::ok &&
            result.length == response.size() &&
            std::memcmp(
                result.payload.data(),
                response.data(),
                response.size()) == 0,
        "multi-frame UDS response preserved");
  }

  {
    FakeClock clock;
    FakeCan client_can{clock};
    FakeCan server_can{clock};
    client_can.connect(server_can);
    server_can.connect(client_can);

    auto iso = isotp_config();
    IsoTpEndpoint client_tp{
        client_can, clock, client_address(), iso};
    IsoTpEndpoint server_tp{
        server_can, clock, server_address(), iso};
    IsoTpDiagnosticTransport diagnostic_transport{client_tp};
    UdsClient client{
        diagnostic_transport, clock, uds_timing()};

    const auto request =
        make_read_data_by_identifier(0xF190U);
    const std::vector<std::byte> wrong_response{
        std::byte{0x50},
        std::byte{0x01}};

    failures += require(
        client.start_request(request) ==
            UdsStatus::in_progress,
        "mismatched SID request starts");

    failures += require(
        pump_server_response(
            client,
            server_tp,
            clock,
            to_vector(request),
            wrong_response),
        "mismatched SID produces completion");

    failures += require(
        client.take_response().status ==
            UdsStatus::protocol_error,
        "mismatched positive SID rejected");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "UDS_CORE_TESTS=PASS\n";
  return 0;
}
