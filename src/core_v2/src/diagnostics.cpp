#include "ecu/core_v2/protocol/j1939/diagnostics.hpp"

#include <array>

namespace ecu::core::v2::protocol::j1939 {
namespace {

DiagnosticLampStatus lamp_status(
    const std::uint8_t value,
    const std::uint8_t shift) noexcept {
  return static_cast<DiagnosticLampStatus>(
      (value >> shift) & 0x03U);
}

DiagnosticLampFlash lamp_flash(
    const std::uint8_t value,
    const std::uint8_t shift) noexcept {
  return static_cast<DiagnosticLampFlash>(
      (value >> shift) & 0x03U);
}

bool single_frame_dtc_absent(
    const transport::CanFrame& frame) noexcept {
  return frame.payload[2U] == std::byte{0U} &&
         frame.payload[3U] == std::byte{0U} &&
         frame.payload[4U] == std::byte{0U} &&
         frame.payload[5U] == std::byte{0U};
}

bool single_frame_padding_valid(
    const transport::CanFrame& frame) noexcept {
  return frame.payload[6U] == std::byte{0xFFU} &&
         frame.payload[7U] == std::byte{0xFFU};
}

DiagnosticDecodeStatus decode_dtc_bytes(
    const std::byte* const bytes,
    DiagnosticTroubleCode& dtc) noexcept {
  if (bytes == nullptr) {
    return DiagnosticDecodeStatus::invalid_argument;
  }

  const auto b0 = std::to_integer<std::uint8_t>(bytes[0U]);
  const auto b1 = std::to_integer<std::uint8_t>(bytes[1U]);
  const auto b2 = std::to_integer<std::uint8_t>(bytes[2U]);
  const auto b3 = std::to_integer<std::uint8_t>(bytes[3U]);

  dtc = {};
  dtc.fmi = static_cast<std::uint8_t>(b2 & 0x1FU);
  dtc.occurrence_count = static_cast<std::uint8_t>(b3 & 0x7FU);
  dtc.conversion_method = (b3 & 0x80U) != 0U;

  if (dtc.conversion_method) {
    return DiagnosticDecodeStatus::unsupported_conversion_method;
  }

  dtc.spn =
      static_cast<std::uint32_t>(b0) |
      (static_cast<std::uint32_t>(b1) << 8U) |
      (static_cast<std::uint32_t>(b2 & 0xE0U) << 11U);
  return DiagnosticDecodeStatus::ok;
}

bool decode_dm_identifier(
    const transport::CanFrame& frame,
    std::uint32_t& pgn,
    std::uint8_t& source_address) noexcept {
  IdentifierFields fields{};
  if (!decode_classic_frame_identifier(frame, fields)) {
    return false;
  }

  pgn = parameter_group_number(fields);
  source_address = fields.source_address;
  return is_dm1_or_dm2_pgn(pgn) &&
         is_claimable_address(source_address);
}

}  // namespace

bool decode_diagnostic_lamps(
    const std::byte lamp_status_byte,
    const std::byte lamp_flash_byte,
    DiagnosticLampState& state) noexcept {
  const auto status =
      std::to_integer<std::uint8_t>(lamp_status_byte);
  const auto flash =
      std::to_integer<std::uint8_t>(lamp_flash_byte);

  state.malfunction_indicator = lamp_status(status, 6U);
  state.red_stop = lamp_status(status, 4U);
  state.amber_warning = lamp_status(status, 2U);
  state.protect = lamp_status(status, 0U);

  state.malfunction_indicator_flash = lamp_flash(flash, 6U);
  state.red_stop_flash = lamp_flash(flash, 4U);
  state.amber_warning_flash = lamp_flash(flash, 2U);
  state.protect_flash = lamp_flash(flash, 0U);
  return true;
}

bool decode_dm_frame(
    const transport::CanFrame& frame,
    DiagnosticMessageInfo& info) noexcept {
  std::uint32_t pgn = 0U;
  std::uint8_t source_address = kNullAddress;
  if (frame.length != 8U ||
      !decode_dm_identifier(frame, pgn, source_address) ||
      !single_frame_padding_valid(frame)) {
    return false;
  }

  DiagnosticMessageInfo decoded{};
  decoded.pgn = pgn;
  decoded.source_address = source_address;
  decoded.transported = false;
  decoded.dtc_count = single_frame_dtc_absent(frame) ? 0U : 1U;
  (void)decode_diagnostic_lamps(
      frame.payload[0U],
      frame.payload[1U],
      decoded.lamps);

  info = decoded;
  return true;
}

bool decode_dm_transport(
    const TpMessage& message,
    DiagnosticMessageInfo& info) noexcept {
  if (!is_dm1_or_dm2_pgn(message.pgn) ||
      !message.broadcast ||
      message.destination_address != kGlobalAddress ||
      !is_claimable_address(message.source_address) ||
      message.size < 10U ||
      ((static_cast<std::size_t>(message.size) - 2U) %
       kDtcEncodedBytes) != 0U) {
    return false;
  }

  DiagnosticMessageInfo decoded{};
  decoded.pgn = message.pgn;
  decoded.source_address = message.source_address;
  decoded.transported = true;
  decoded.dtc_count =
      (static_cast<std::size_t>(message.size) - 2U) /
      kDtcEncodedBytes;
  (void)decode_diagnostic_lamps(
      message.data[0U],
      message.data[1U],
      decoded.lamps);

  info = decoded;
  return true;
}

DiagnosticDecodeStatus decode_dtc(
    const transport::CanFrame& frame,
    const std::size_t index,
    DiagnosticTroubleCode& dtc) noexcept {
  DiagnosticMessageInfo info{};
  if (!decode_dm_frame(frame, info) ||
      index >= info.dtc_count) {
    return DiagnosticDecodeStatus::invalid_argument;
  }

  const auto offset = 2U + (index * kDtcEncodedBytes);
  return decode_dtc_bytes(
      frame.payload.data() + offset,
      dtc);
}

DiagnosticDecodeStatus decode_dtc(
    const TpMessage& message,
    const std::size_t index,
    DiagnosticTroubleCode& dtc) noexcept {
  DiagnosticMessageInfo info{};
  if (!decode_dm_transport(message, info) ||
      index >= info.dtc_count) {
    return DiagnosticDecodeStatus::invalid_argument;
  }

  const auto offset = 2U + (index * kDtcEncodedBytes);
  return decode_dtc_bytes(
      message.data.data() + offset,
      dtc);
}

}  // namespace ecu::core::v2::protocol::j1939
