#pragma once

#include "ecu/core/protocol/uds/uds_client.hpp"
#include "ecu/core/transport/can_types.hpp"
#include "ecu/sac/sac_runtime.hpp"

#include <cstdint>

namespace ecu::sac {

enum class SacRuntimeStatus : std::uint8_t {
  idle,
  in_progress,
  voltage_updated,
  busy,
  uds_error,
  invalid_response,
};

class SacRuntimeMonitor {
 public:
  explicit SacRuntimeMonitor(
      core::protocol::uds::UdsClient& uds) noexcept;

  bool ingest_can_frame(
      const core::transport::CanFrame& frame) noexcept;

  SacRuntimeStatus start_voltage_read() noexcept;
  SacRuntimeStatus poll() noexcept;

  [[nodiscard]] const SacPressureState& pressure() const noexcept;
  [[nodiscard]] const SacVoltageState& voltage() const noexcept;
  [[nodiscard]] SacRuntimeStatus status() const noexcept;
  [[nodiscard]] std::uint8_t last_nrc() const noexcept;

  void reset() noexcept;

 private:
  core::protocol::uds::UdsClient& uds_;
  SacPressureState pressure_{};
  SacVoltageState voltage_{};
  SacRuntimeStatus status_{SacRuntimeStatus::idle};
  bool voltage_request_active_{false};
  std::uint8_t last_nrc_{0U};
};

}  // namespace ecu::sac
