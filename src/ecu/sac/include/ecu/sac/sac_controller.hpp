#pragma once

#include "ecu/sac/sac_dtc_reader.hpp"
#include "ecu/sac/sac_identification.hpp"
#include "ecu/sac/sac_runtime_monitor.hpp"

#include <cstdint>

namespace ecu::sac {

enum class SacOperation : std::uint8_t {
  idle,
  identification,
  dtc_read,
  voltage_read,
};

enum class SacControllerStatus : std::uint8_t {
  idle,
  in_progress,
  identification_ready,
  dtc_ready,
  voltage_ready,
  busy,
  error,
};

enum class SacControllerError : std::uint8_t {
  none,
  uds_error,
  invalid_response,
  capacity_exceeded,
};

class SacController {
 public:
  explicit SacController(
      core::protocol::uds::UdsClient& uds) noexcept;

  SacControllerStatus start_identification() noexcept;
  SacControllerStatus start_dtc_read(
      std::uint8_t status_mask = 0xFFU) noexcept;
  SacControllerStatus start_voltage_read() noexcept;

  bool ingest_can_frame(
      const core::transport::CanFrame& frame) noexcept;

  SacControllerStatus poll() noexcept;

  [[nodiscard]] bool busy() const noexcept;
  [[nodiscard]] SacOperation operation() const noexcept;
  [[nodiscard]] SacControllerStatus status() const noexcept;
  [[nodiscard]] SacControllerError error() const noexcept;
  [[nodiscard]] std::uint8_t last_nrc() const noexcept;

  [[nodiscard]] const SacIdentificationResult& identification()
      const noexcept;
  [[nodiscard]] const SacDtcReadResult& dtcs() const noexcept;
  [[nodiscard]] const SacPressureState& pressure() const noexcept;
  [[nodiscard]] const SacVoltageState& voltage() const noexcept;

  void reset() noexcept;

 private:
  SacIdentification identification_;
  SacDtcReader dtc_reader_;
  SacRuntimeMonitor runtime_;

  SacOperation operation_{SacOperation::idle};
  SacControllerStatus status_{SacControllerStatus::idle};
  SacControllerError error_{SacControllerError::none};
  std::uint8_t last_nrc_{0U};

  void begin_operation(SacOperation operation) noexcept;
  void complete_operation(SacControllerStatus status) noexcept;
  void fail_operation(
      SacControllerError error,
      std::uint8_t nrc = 0U) noexcept;
};

}  // namespace ecu::sac
