#include "ecu/sac/sac_controller.hpp"

namespace ecu::sac {

SacController::SacController(
    core::protocol::uds::UdsClient& uds) noexcept
    : identification_(uds),
      dtc_reader_(uds),
      runtime_(uds) {}

SacControllerStatus SacController::start_identification() noexcept {
  if (busy()) {
    return SacControllerStatus::busy;
  }

  const auto result = identification_.start();
  if (result != SacIdentificationStatus::in_progress) {
    fail_operation(SacControllerError::uds_error);
    return status_;
  }

  begin_operation(SacOperation::identification);
  return status_;
}

SacControllerStatus SacController::start_dtc_read(
    const std::uint8_t status_mask) noexcept {
  if (busy()) {
    return SacControllerStatus::busy;
  }

  const auto result = dtc_reader_.start(status_mask);
  if (result != SacDtcReadStatus::in_progress) {
    fail_operation(SacControllerError::uds_error);
    return status_;
  }

  begin_operation(SacOperation::dtc_read);
  return status_;
}

SacControllerStatus SacController::start_voltage_read() noexcept {
  if (busy()) {
    return SacControllerStatus::busy;
  }

  const auto result = runtime_.start_voltage_read();
  if (result != SacRuntimeStatus::in_progress) {
    fail_operation(SacControllerError::uds_error);
    return status_;
  }

  begin_operation(SacOperation::voltage_read);
  return status_;
}

bool SacController::ingest_can_frame(
    const core::transport::CanFrame& frame) noexcept {
  return runtime_.ingest_can_frame(frame);
}

SacControllerStatus SacController::poll() noexcept {
  switch (operation_) {
    case SacOperation::idle:
      return status_;

    case SacOperation::identification: {
      const auto result = identification_.poll();

      if (result == SacIdentificationStatus::done) {
        complete_operation(
            SacControllerStatus::identification_ready);
      } else if (result == SacIdentificationStatus::uds_error) {
        fail_operation(
            SacControllerError::uds_error,
            identification_.last_nrc());
      } else if (
          result == SacIdentificationStatus::invalid_response) {
        fail_operation(SacControllerError::invalid_response);
      }

      return status_;
    }

    case SacOperation::dtc_read: {
      const auto result = dtc_reader_.poll();

      if (result == SacDtcReadStatus::done) {
        complete_operation(SacControllerStatus::dtc_ready);
      } else if (result == SacDtcReadStatus::uds_error) {
        fail_operation(
            SacControllerError::uds_error,
            dtc_reader_.last_nrc());
      } else if (result == SacDtcReadStatus::invalid_response) {
        fail_operation(SacControllerError::invalid_response);
      } else if (
          result == SacDtcReadStatus::capacity_exceeded) {
        fail_operation(SacControllerError::capacity_exceeded);
      }

      return status_;
    }

    case SacOperation::voltage_read: {
      const auto result = runtime_.poll();

      if (result == SacRuntimeStatus::voltage_updated) {
        complete_operation(SacControllerStatus::voltage_ready);
      } else if (result == SacRuntimeStatus::uds_error) {
        fail_operation(
            SacControllerError::uds_error,
            runtime_.last_nrc());
      } else if (
          result == SacRuntimeStatus::invalid_response) {
        fail_operation(SacControllerError::invalid_response);
      }

      return status_;
    }
  }

  fail_operation(SacControllerError::uds_error);
  return status_;
}

bool SacController::busy() const noexcept {
  return operation_ != SacOperation::idle;
}

SacOperation SacController::operation() const noexcept {
  return operation_;
}

SacControllerStatus SacController::status() const noexcept {
  return status_;
}

SacControllerError SacController::error() const noexcept {
  return error_;
}

std::uint8_t SacController::last_nrc() const noexcept {
  return last_nrc_;
}

const SacIdentificationResult& SacController::identification()
    const noexcept {
  return identification_.result();
}

const SacDtcReadResult& SacController::dtcs() const noexcept {
  return dtc_reader_.result();
}

const SacPressureState& SacController::pressure() const noexcept {
  return runtime_.pressure();
}

const SacVoltageState& SacController::voltage() const noexcept {
  return runtime_.voltage();
}

void SacController::reset() noexcept {
  identification_.reset();
  dtc_reader_.reset();
  runtime_.reset();
  operation_ = SacOperation::idle;
  status_ = SacControllerStatus::idle;
  error_ = SacControllerError::none;
  last_nrc_ = 0U;
}

void SacController::begin_operation(
    const SacOperation operation) noexcept {
  operation_ = operation;
  status_ = SacControllerStatus::in_progress;
  error_ = SacControllerError::none;
  last_nrc_ = 0U;
}

void SacController::complete_operation(
    const SacControllerStatus status) noexcept {
  operation_ = SacOperation::idle;
  status_ = status;
  error_ = SacControllerError::none;
  last_nrc_ = 0U;
}

void SacController::fail_operation(
    const SacControllerError error,
    const std::uint8_t nrc) noexcept {
  operation_ = SacOperation::idle;
  status_ = SacControllerStatus::error;
  error_ = error;
  last_nrc_ = nrc;
}

}  // namespace ecu::sac
