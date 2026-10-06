#include "ecu/core/transport/can_types.hpp"

namespace ecu::core::transport {
namespace {

constexpr std::uint32_t kMaxStandardIdentifier = 0x7FFU;
constexpr std::uint32_t kMaxExtendedIdentifier = 0x1FFFFFFFU;

bool valid_identifier(
    const std::uint32_t identifier,
    const CanIdentifierFormat format) noexcept {
  switch (format) {
    case CanIdentifierFormat::standard_11_bit:
      return identifier <= kMaxStandardIdentifier;
    case CanIdentifierFormat::extended_29_bit:
      return identifier <= kMaxExtendedIdentifier;
  }
  return false;
}

bool valid_fd_wire_length(const std::uint8_t length) noexcept {
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

bool is_valid_can_frame(const CanFrame& frame) noexcept {
  if (!valid_identifier(frame.identifier, frame.identifier_format)) {
    return false;
  }

  if (frame.format == CanFrameFormat::classic) {
    if (frame.length > 8U) {
      return false;
    }
    if (frame.bit_rate_switch || frame.error_state_indicator) {
      return false;
    }
    return true;
  }

  if (frame.type == CanFrameType::remote) {
    return false;
  }

  return valid_fd_wire_length(frame.length);
}

bool is_valid_can_channel_config(const CanChannelConfig& config) noexcept {
  if (config.nominal_bitrate == 0U) {
    return false;
  }

  if (!config.fd_enabled) {
    return config.data_bitrate == 0U;
  }

  return config.data_bitrate != 0U;
}

bool capabilities_support(
    const CanCapabilities& capabilities,
    const CanChannelConfig& config) noexcept {
  if (!is_valid_can_channel_config(config)) {
    return false;
  }

  if (!capabilities.classic_can) {
    return false;
  }

  if (!config.fd_enabled) {
    return capabilities.max_payload_bytes >= 8U;
  }

  return capabilities.can_fd &&
         capabilities.max_payload_bytes >= 64U;
}

}  // namespace ecu::core::transport
