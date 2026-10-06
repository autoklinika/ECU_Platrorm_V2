#include "ecu/platform/linux/socketcan/detail/socketcan_codec.hpp"

#include <algorithm>
#include <cstddef>

namespace ecu::platform::linux::socketcan::detail {
namespace {

using core::transport::CanFrame;
using core::transport::CanIdentifierFormat;
using core::transport::CanStatus;

canid_t encode_identifier(const CanFrame& source) noexcept {
  canid_t identifier = source.identifier;
  if (source.identifier_format == CanIdentifierFormat::extended_29_bit) {
    identifier |= CAN_EFF_FLAG;
  }
  if (source.type == core::transport::CanFrameType::remote) {
    identifier |= CAN_RTR_FLAG;
  }
  return identifier;
}

void decode_identifier(const canid_t source, CanFrame& target) noexcept {
  target.identifier_format =
      (source & CAN_EFF_FLAG) != 0U
          ? CanIdentifierFormat::extended_29_bit
          : CanIdentifierFormat::standard_11_bit;

  target.identifier =
      source & (target.identifier_format == CanIdentifierFormat::extended_29_bit
                    ? CAN_EFF_MASK
                    : CAN_SFF_MASK);

  target.type =
      (source & CAN_RTR_FLAG) != 0U
          ? core::transport::CanFrameType::remote
          : core::transport::CanFrameType::data;
}

}  // namespace

CanStatus encode_classic_frame(
    const CanFrame& source,
    can_frame& target) noexcept {
  if (!core::transport::is_valid_can_frame(source) ||
      source.format != core::transport::CanFrameFormat::classic) {
    return CanStatus::invalid_argument;
  }

  target = {};
  target.can_id = encode_identifier(source);
  target.len = source.length;

  if (source.type == core::transport::CanFrameType::data) {
    std::copy_n(source.payload.begin(), source.length, target.data);
  }

  return CanStatus::ok;
}

CanStatus encode_fd_frame(
    const CanFrame& source,
    canfd_frame& target) noexcept {
  if (!core::transport::is_valid_can_frame(source) ||
      source.format != core::transport::CanFrameFormat::fd) {
    return CanStatus::invalid_argument;
  }

  target = {};
  target.can_id = encode_identifier(source);
  target.len = source.length;

  if (source.bit_rate_switch) {
    target.flags |= CANFD_BRS;
  }
  if (source.error_state_indicator) {
    target.flags |= CANFD_ESI;
  }

  std::copy_n(source.payload.begin(), source.length, target.data);
  return CanStatus::ok;
}

CanStatus decode_classic_frame(
    const can_frame& source,
    const core::time::MonotonicTime timestamp,
    core::transport::CanReceiveResult& target) noexcept {
  if ((source.can_id & CAN_ERR_FLAG) != 0U || source.len > CAN_MAX_DLEN) {
    return CanStatus::invalid_argument;
  }

  target = {};
  target.status = CanStatus::ok;
  target.value.timestamp = timestamp;
  target.value.frame.format = core::transport::CanFrameFormat::classic;
  decode_identifier(source.can_id, target.value.frame);
  target.value.frame.length = source.len;

  if (target.value.frame.type == core::transport::CanFrameType::data) {
    std::copy_n(
        source.data,
        source.len,
        target.value.frame.payload.begin());
  }

  return CanStatus::ok;
}

CanStatus decode_fd_frame(
    const canfd_frame& source,
    const core::time::MonotonicTime timestamp,
    core::transport::CanReceiveResult& target) noexcept {
  if ((source.can_id & CAN_ERR_FLAG) != 0U ||
      source.len > CANFD_MAX_DLEN) {
    return CanStatus::invalid_argument;
  }

  target = {};
  target.status = CanStatus::ok;
  target.value.timestamp = timestamp;
  target.value.frame.format = core::transport::CanFrameFormat::fd;
  decode_identifier(source.can_id, target.value.frame);

  if (target.value.frame.type == core::transport::CanFrameType::remote) {
    return CanStatus::invalid_argument;
  }

  target.value.frame.length = source.len;
  target.value.frame.bit_rate_switch = (source.flags & CANFD_BRS) != 0U;
  target.value.frame.error_state_indicator = (source.flags & CANFD_ESI) != 0U;
  std::copy_n(
      source.data,
      source.len,
      target.value.frame.payload.begin());

  if (!core::transport::is_valid_can_frame(target.value.frame)) {
    return CanStatus::invalid_argument;
  }

  return CanStatus::ok;
}

}  // namespace ecu::platform::linux::socketcan::detail
