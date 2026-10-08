#pragma once

#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core/transport/diagnostic_transport.hpp"

namespace ecu::core::protocol::isotp {

class IsoTpDiagnosticTransport final
    : public transport::IDiagnosticTransport {
 public:
  explicit IsoTpDiagnosticTransport(
      IsoTpEndpoint& endpoint) noexcept;

  [[nodiscard]] bool valid() const noexcept override;

  transport::DiagnosticTransportStatus start_send(
      const std::byte* payload,
      std::size_t length) noexcept override;

  transport::DiagnosticTransportStatus poll() noexcept override;

  [[nodiscard]] bool tx_busy() const noexcept override;

  [[nodiscard]] transport::DiagnosticTransportStatus last_tx_status()
      const noexcept override;

  [[nodiscard]] bool has_received() const noexcept override;
  [[nodiscard]] std::size_t received_size() const noexcept override;

  transport::DiagnosticTransportStatus take_received(
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length) noexcept override;

  void reset() noexcept override;

 private:
  IsoTpEndpoint& endpoint_;

  [[nodiscard]] static transport::DiagnosticTransportStatus map_status(
      IsoTpStatus status) noexcept;
};

}  // namespace ecu::core::protocol::isotp
