#pragma once

#include <cstddef>
#include <cstdint>

namespace ecu::core::transport {

enum class DiagnosticTransportStatus : std::uint8_t {
  ok,
  in_progress,
  idle,
  busy,
  would_block,
  invalid_argument,
  payload_too_large,
  timeout,
  protocol_error,
  transport_error,
  bus_off,
};

class IDiagnosticTransport {
 public:
  virtual ~IDiagnosticTransport() = default;

  [[nodiscard]] virtual bool valid() const noexcept = 0;

  virtual DiagnosticTransportStatus start_send(
      const std::byte* payload,
      std::size_t length) noexcept = 0;

  virtual DiagnosticTransportStatus poll() noexcept = 0;

  [[nodiscard]] virtual bool tx_busy() const noexcept = 0;
  [[nodiscard]] virtual DiagnosticTransportStatus last_tx_status()
      const noexcept = 0;

  [[nodiscard]] virtual bool has_received() const noexcept = 0;
  [[nodiscard]] virtual std::size_t received_size() const noexcept = 0;

  virtual DiagnosticTransportStatus take_received(
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length) noexcept = 0;

  virtual void reset() noexcept = 0;
};

}  // namespace ecu::core::transport
