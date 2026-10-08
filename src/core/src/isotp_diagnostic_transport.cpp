#include "ecu/core/protocol/isotp/isotp_diagnostic_transport.hpp"

#include <cstring>

namespace ecu::core::protocol::isotp {

IsoTpDiagnosticTransport::IsoTpDiagnosticTransport(
    IsoTpEndpoint& endpoint) noexcept
    : endpoint_(endpoint) {}

bool IsoTpDiagnosticTransport::valid() const noexcept {
  return endpoint_.valid();
}

transport::DiagnosticTransportStatus
IsoTpDiagnosticTransport::start_send(
    const std::byte* payload,
    const std::size_t length) noexcept {
  return map_status(endpoint_.start_send(payload, length));
}

transport::DiagnosticTransportStatus
IsoTpDiagnosticTransport::poll() noexcept {
  return map_status(endpoint_.poll());
}

bool IsoTpDiagnosticTransport::tx_busy() const noexcept {
  return endpoint_.tx_busy();
}

transport::DiagnosticTransportStatus
IsoTpDiagnosticTransport::last_tx_status() const noexcept {
  return map_status(endpoint_.last_tx_status());
}

bool IsoTpDiagnosticTransport::has_received() const noexcept {
  return endpoint_.has_received();
}

std::size_t IsoTpDiagnosticTransport::received_size() const noexcept {
  return endpoint_.peek_received_length();
}

transport::DiagnosticTransportStatus
IsoTpDiagnosticTransport::take_received(
    std::byte* destination,
    const std::size_t capacity,
    std::size_t& length) noexcept {
  length = 0U;

  if (destination == nullptr || !endpoint_.has_received()) {
    return transport::DiagnosticTransportStatus::invalid_argument;
  }

  const auto required = endpoint_.peek_received_length();
  if (required > capacity) {
    return transport::DiagnosticTransportStatus::payload_too_large;
  }

  const auto received = endpoint_.take_received();
  if (received.status != IsoTpStatus::ok) {
    return map_status(received.status);
  }

  std::memcpy(destination, received.payload.data(), received.length);
  length = received.length;
  return transport::DiagnosticTransportStatus::ok;
}

void IsoTpDiagnosticTransport::reset() noexcept {
  endpoint_.reset();
}

transport::DiagnosticTransportStatus
IsoTpDiagnosticTransport::map_status(
    const IsoTpStatus status) noexcept {
  switch (status) {
    case IsoTpStatus::ok:
      return transport::DiagnosticTransportStatus::ok;
    case IsoTpStatus::in_progress:
      return transport::DiagnosticTransportStatus::in_progress;
    case IsoTpStatus::idle:
      return transport::DiagnosticTransportStatus::idle;
    case IsoTpStatus::busy:
      return transport::DiagnosticTransportStatus::busy;
    case IsoTpStatus::would_block:
      return transport::DiagnosticTransportStatus::would_block;
    case IsoTpStatus::invalid_argument:
      return transport::DiagnosticTransportStatus::invalid_argument;
    case IsoTpStatus::payload_too_large:
      return transport::DiagnosticTransportStatus::payload_too_large;
    case IsoTpStatus::timeout:
      return transport::DiagnosticTransportStatus::timeout;
    case IsoTpStatus::sequence_error:
    case IsoTpStatus::flow_control_overflow:
    case IsoTpStatus::protocol_error:
      return transport::DiagnosticTransportStatus::protocol_error;
    case IsoTpStatus::transport_error:
      return transport::DiagnosticTransportStatus::transport_error;
    case IsoTpStatus::bus_off:
      return transport::DiagnosticTransportStatus::bus_off;
  }

  return transport::DiagnosticTransportStatus::transport_error;
}

}  // namespace ecu::core::protocol::isotp
