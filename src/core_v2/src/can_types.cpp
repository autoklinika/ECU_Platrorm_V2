#include "ecu/core_v2/transport/can_types.hpp"

namespace ecu::core::v2::transport {

namespace {

constexpr std::uint32_t kMaxStandardIdentifier = 0x7FFU;
constexpr std::uint32_t kMaxExtendedIdentifier = 0x1FFFFFFFU;

[[nodiscard]] constexpr bool valid_classic_length(
    const std::uint8_t length) noexcept {
  return length <= 8U;
}

[[nodiscard]] constexpr bool valid_fd_length(
    const std::uint8_t length) noexcept {
  return length <= 8U || length == 12U || length == 16U ||
         length == 20U || length == 24U || length == 32U ||
         length == 48U || length == 64U;
}

}  // namespace

bool is_valid_can_frame(const CanFrame& frame) noexcept {
  if ((frame.identifier_format != CanIdentifierFormat::standard_11_bit &&
       frame.identifier_format != CanIdentifierFormat::extended_29_bit) ||
      (frame.format != CanFrameFormat::classic && frame.format != CanFrameFormat::fd) ||
      (frame.type != CanFrameType::data && frame.type != CanFrameType::remote)) {
    return false;
  }
  const auto max_identifier =
      frame.identifier_format == CanIdentifierFormat::standard_11_bit
          ? kMaxStandardIdentifier
          : kMaxExtendedIdentifier;

  if (frame.identifier > max_identifier) {
    return false;
  }

  if (frame.format == CanFrameFormat::classic) {
    if (!valid_classic_length(frame.length) ||
        frame.bit_rate_switch ||
        frame.error_state_indicator) {
      return false;
    }
  } else {
    if (!valid_fd_length(frame.length) ||
        frame.type == CanFrameType::remote) {
      return false;
    }
  }

  return true;
}

bool is_valid_can_channel_config(
    const CanChannelConfig& config) noexcept {
  if ((config.mode != CanMode::normal && config.mode != CanMode::listen_only) ||
      config.nominal_bitrate == 0U) {
    return false;
  }

  if (config.fd_enabled) {
    return config.data_bitrate != 0U;
  }

  return config.data_bitrate == 0U;
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

  if (config.fd_enabled && !capabilities.can_fd) {
    return false;
  }

  if (config.mode == CanMode::listen_only &&
      !capabilities.listen_only) {
    return false;
  }

  return true;
}

bool capabilities_support_frame(
    const CanCapabilities& capabilities,
    const CanFrame& frame) noexcept {
  if (!is_valid_can_frame(frame)) {
    return false;
  }

  if (frame.format == CanFrameFormat::fd) {
    if (!capabilities.can_fd ||
        capabilities.max_payload_bytes < frame.length) {
      return false;
    }
    if (frame.bit_rate_switch && !capabilities.bit_rate_switch) {
      return false;
    }
    return true;
  }

  return capabilities.classic_can &&
         capabilities.max_payload_bytes >= frame.length;
}

}  // namespace ecu::core::v2::transport
