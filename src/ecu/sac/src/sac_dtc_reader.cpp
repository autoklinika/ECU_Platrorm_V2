#include "ecu/sac/sac_dtc_reader.hpp"

#include "ecu/core/protocol/uds/uds_services.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::sac {
namespace {

std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

}  // namespace

SacDtcReader::SacDtcReader(
    core::protocol::uds::UdsClient& uds) noexcept
    : uds_(uds) {}

SacDtcReadStatus SacDtcReader::start(
    const std::uint8_t status_mask) noexcept {
  if (status_ == SacDtcReadStatus::in_progress) {
    return SacDtcReadStatus::busy;
  }

  uds_.reset();
  result_ = {};
  requested_status_mask_ = status_mask;
  last_nrc_ = 0U;
  step_ = Step::request_session;
  status_ = SacDtcReadStatus::in_progress;
  return status_;
}

SacDtcReadStatus SacDtcReader::poll() noexcept {
  if (step_ == Step::idle) {
    return SacDtcReadStatus::idle;
  }

  if (step_ == Step::done || step_ == Step::error) {
    return status_;
  }

  switch (step_) {
    case Step::request_session: {
      const auto request =
          core::protocol::uds::make_diagnostic_session_control(0x03U);
      const auto status = uds_.start_request(request);

      if (status ==
          core::protocol::uds::UdsStatus::in_progress) {
        step_ = Step::wait_session;
        return status_;
      }

      if (status == core::protocol::uds::UdsStatus::busy) {
        return status_;
      }

      fail(SacDtcReadStatus::uds_error);
      return status_;
    }

    case Step::wait_session:
      return handle_session_response();

    case Step::request_dtc: {
      const auto request =
          core::protocol::uds::make_read_dtc_information_by_status_mask(
              requested_status_mask_);

      const auto status = uds_.start_request(request);

      if (status ==
          core::protocol::uds::UdsStatus::in_progress) {
        step_ = Step::wait_dtc;
        return status_;
      }

      if (status == core::protocol::uds::UdsStatus::busy) {
        return status_;
      }

      fail(SacDtcReadStatus::uds_error);
      return status_;
    }

    case Step::wait_dtc:
      return handle_dtc_response();

    case Step::idle:
    case Step::done:
    case Step::error:
      break;
  }

  fail(SacDtcReadStatus::uds_error);
  return status_;
}

SacDtcReadStatus SacDtcReader::status() const noexcept {
  return status_;
}

const SacDtcReadResult& SacDtcReader::result() const noexcept {
  return result_;
}

std::uint8_t SacDtcReader::last_nrc() const noexcept {
  return last_nrc_;
}

void SacDtcReader::reset() noexcept {
  uds_.reset();
  step_ = Step::idle;
  status_ = SacDtcReadStatus::idle;
  result_ = {};
  requested_status_mask_ = 0xFFU;
  last_nrc_ = 0U;
}

SacDtcReadStatus SacDtcReader::handle_session_response() noexcept {
  const auto poll_status = uds_.poll();

  if (!uds_.has_response()) {
    if (poll_status ==
            core::protocol::uds::UdsStatus::in_progress ||
        poll_status ==
            core::protocol::uds::UdsStatus::idle) {
      return status_;
    }

    fail(SacDtcReadStatus::uds_error);
    return status_;
  }

  const auto response = uds_.take_response();

  if (response.status ==
      core::protocol::uds::UdsStatus::negative_response) {
    fail(
        SacDtcReadStatus::uds_error,
        response.negative_response_code);
    return status_;
  }

  if (response.status !=
          core::protocol::uds::UdsStatus::ok ||
      response.length < 2U ||
      byte_value(response.payload[0]) != 0x50U ||
      byte_value(response.payload[1]) != 0x03U) {
    fail(SacDtcReadStatus::invalid_response);
    return status_;
  }

  step_ = Step::request_dtc;
  return status_;
}

SacDtcReadStatus SacDtcReader::handle_dtc_response() noexcept {
  const auto poll_status = uds_.poll();

  if (!uds_.has_response()) {
    if (poll_status ==
            core::protocol::uds::UdsStatus::in_progress ||
        poll_status ==
            core::protocol::uds::UdsStatus::idle) {
      return status_;
    }

    fail(SacDtcReadStatus::uds_error);
    return status_;
  }

  const auto response = uds_.take_response();

  if (response.status ==
      core::protocol::uds::UdsStatus::negative_response) {
    fail(
        SacDtcReadStatus::uds_error,
        response.negative_response_code);
    return status_;
  }

  if (response.status !=
          core::protocol::uds::UdsStatus::ok ||
      response.length < 3U ||
      byte_value(response.payload[0]) != 0x59U ||
      byte_value(response.payload[1]) != 0x02U) {
    fail(SacDtcReadStatus::invalid_response);
    return status_;
  }

  const std::size_t record_bytes =
      response.length - 3U;

  if ((record_bytes % 4U) != 0U) {
    fail(SacDtcReadStatus::invalid_response);
    return status_;
  }

  const std::size_t record_count = record_bytes / 4U;
  if (record_count > result_.records.size()) {
    fail(SacDtcReadStatus::capacity_exceeded);
    return status_;
  }

  result_ = {};
  result_.status_availability_mask =
      byte_value(response.payload[2]);
  result_.count = record_count;

  std::size_t offset = 3U;
  for (std::size_t index = 0U;
       index < record_count;
       ++index) {
    const std::uint32_t code =
        (static_cast<std::uint32_t>(
             byte_value(response.payload[offset]))
         << 16U) |
        (static_cast<std::uint32_t>(
             byte_value(response.payload[offset + 1U]))
         << 8U) |
        static_cast<std::uint32_t>(
            byte_value(response.payload[offset + 2U]));

    result_.records[index] =
        SacDtcRecord{
            code,
            byte_value(response.payload[offset + 3U])};

    offset += 4U;
  }

  step_ = Step::done;
  status_ = SacDtcReadStatus::done;
  return status_;
}

void SacDtcReader::fail(
    const SacDtcReadStatus status,
    const std::uint8_t nrc) noexcept {
  step_ = Step::error;
  status_ = status;
  last_nrc_ = nrc;
}

}  // namespace ecu::sac
