#include "ecu/core/protocol/isotp/isotp_types.hpp"

namespace ecu::core::protocol::isotp {
namespace {

bool valid_identifier(
    const std::uint32_t identifier,
    const transport::CanIdentifierFormat format) noexcept {
  switch (format) {
    case transport::CanIdentifierFormat::standard_11_bit:
      return identifier <= 0x7FFU;
    case transport::CanIdentifierFormat::extended_29_bit:
      return identifier <= 0x1FFFFFFFU;
  }
  return false;
}

bool valid_fd_data_length(const std::uint8_t length) noexcept {
  if (length <= 8U) {
    return true;
  }

  switch (length) {
    case 12U:
    case 16U:
    case 20U:
    case 24U:
    case 32U:
    case 48U:
    case 64U:
      return true;
    default:
      return false;
  }
}

}  // namespace

bool is_valid_isotp_address(const IsoTpAddress& address) noexcept {
  return valid_identifier(address.tx_id, address.identifier_format) &&
         valid_identifier(address.rx_id, address.identifier_format) &&
         address.tx_id != address.rx_id;
}

bool is_valid_isotp_config(const IsoTpConfig& config) noexcept {
  if (config.flow_control_timeout.count() <= 0 ||
      config.consecutive_frame_timeout.count() <= 0) {
    return false;
  }

  bool stmin_valid = false;
  static_cast<void>(decode_stmin(config.rx_stmin, stmin_valid));
  if (!stmin_valid) {
    return false;
  }

  if (config.frame_format == transport::CanFrameFormat::classic) {
    return config.tx_data_length == 8U &&
           !config.bit_rate_switch;
  }

  return valid_fd_data_length(config.tx_data_length) &&
         config.tx_data_length >= 8U;
}

std::chrono::nanoseconds decode_stmin(
    const std::uint8_t encoded,
    bool& valid) noexcept {
  if (encoded <= 0x7FU) {
    valid = true;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::milliseconds{encoded});
  }

  if (encoded >= 0xF1U && encoded <= 0xF9U) {
    valid = true;
    const auto hundreds_of_microseconds =
        static_cast<std::uint32_t>(encoded - 0xF0U);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::microseconds{
            hundreds_of_microseconds * 100U});
  }

  valid = false;
  return std::chrono::nanoseconds{0};
}

}  // namespace ecu::core::protocol::isotp
