#pragma once

#include "ecu/core/protocol/uds/uds_types.hpp"
#include "ecu/core/time/monotonic_clock.hpp"
#include "ecu/core/transport/diagnostic_transport.hpp"

#include <array>
#include <cstddef>

namespace ecu::core::protocol::uds {

class UdsClient {
 public:
  UdsClient(
      transport::IDiagnosticTransport& transport,
      const time::IMonotonicClock& clock,
      UdsTiming timing) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool busy() const noexcept;

  UdsStatus start_request(
      const std::byte* payload,
      std::size_t length) noexcept;

  UdsStatus start_request(
      const UdsRequest& request) noexcept;

  UdsStatus poll() noexcept;

  [[nodiscard]] bool has_response() const noexcept;
  [[nodiscard]] UdsResponse take_response() noexcept;

  [[nodiscard]] UdsTiming timing() const noexcept;
  void set_timing(UdsTiming timing) noexcept;

  void reset() noexcept;

 private:
  enum class State : std::uint8_t {
    idle,
    sending,
    waiting_p2,
    waiting_p2_star,
    complete,
  };

  transport::IDiagnosticTransport& transport_;
  const time::IMonotonicClock& clock_;
  UdsTiming timing_;
  bool valid_{false};

  State state_{State::idle};
  std::uint8_t request_sid_{0U};
  time::MonotonicTime deadline_{0};
  UdsResponse response_{};
  bool response_ready_{false};
  std::array<std::byte, kMaxUdsPayloadSize> transport_rx_buffer_{};

  [[nodiscard]] UdsStatus handle_transport_response(
      const std::byte* payload,
      std::size_t length,
      time::MonotonicTime now) noexcept;

  [[nodiscard]] UdsStatus map_transport_status(
      transport::DiagnosticTransportStatus status) const noexcept;

  void complete_with_status(UdsStatus status) noexcept;
};

}  // namespace ecu::core::protocol::uds
