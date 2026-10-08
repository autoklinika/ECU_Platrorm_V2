#include "ecu/sac/sac_runtime_monitor.hpp"

#include "ecu/core/protocol/uds/uds_services.hpp"
#include "ecu/sac/sac_profile.hpp"

namespace ecu::sac {

SacRuntimeMonitor::SacRuntimeMonitor(
    core::protocol::uds::UdsClient& uds) noexcept
    : uds_(uds) {}

bool SacRuntimeMonitor::ingest_can_frame(
    const core::transport::CanFrame& frame) noexcept {
  return decode_pressure_broadcast(frame, pressure_);
}

SacRuntimeStatus SacRuntimeMonitor::start_voltage_read() noexcept {
  if (voltage_request_active_ || uds_.busy()) {
    return SacRuntimeStatus::busy;
  }

  const auto request =
      core::protocol::uds::make_read_data_by_identifier(kDidVoltage);

  const auto status = uds_.start_request(request);
  if (status != core::protocol::uds::UdsStatus::in_progress) {
    status_ = SacRuntimeStatus::uds_error;
    return status_;
  }

  voltage_request_active_ = true;
  last_nrc_ = 0U;
  status_ = SacRuntimeStatus::in_progress;
  return status_;
}

SacRuntimeStatus SacRuntimeMonitor::poll() noexcept {
  if (!voltage_request_active_) {
    return status_;
  }

  const auto poll_status = uds_.poll();

  if (!uds_.has_response()) {
    if (poll_status ==
            core::protocol::uds::UdsStatus::in_progress ||
        poll_status ==
            core::protocol::uds::UdsStatus::idle) {
      return SacRuntimeStatus::in_progress;
    }

    voltage_request_active_ = false;
    status_ = SacRuntimeStatus::uds_error;
    return status_;
  }

  const auto response = uds_.take_response();
  voltage_request_active_ = false;

  if (response.status ==
      core::protocol::uds::UdsStatus::negative_response) {
    last_nrc_ = response.negative_response_code;
    status_ = SacRuntimeStatus::uds_error;
    return status_;
  }

  if (!decode_voltage_did_response(
          response,
          voltage_)) {
    status_ = SacRuntimeStatus::invalid_response;
    return status_;
  }

  status_ = SacRuntimeStatus::voltage_updated;
  return status_;
}

const SacPressureState& SacRuntimeMonitor::pressure() const noexcept {
  return pressure_;
}

const SacVoltageState& SacRuntimeMonitor::voltage() const noexcept {
  return voltage_;
}

SacRuntimeStatus SacRuntimeMonitor::status() const noexcept {
  return status_;
}

std::uint8_t SacRuntimeMonitor::last_nrc() const noexcept {
  return last_nrc_;
}

void SacRuntimeMonitor::reset() noexcept {
  uds_.reset();
  pressure_ = {};
  voltage_ = {};
  status_ = SacRuntimeStatus::idle;
  voltage_request_active_ = false;
  last_nrc_ = 0U;
}

}  // namespace ecu::sac
