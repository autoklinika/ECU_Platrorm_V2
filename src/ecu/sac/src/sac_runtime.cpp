#include "ecu/sac/sac_runtime.hpp"

#include "ecu/sac/sac_profile.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::sac {
namespace {

std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

}  // namespace

std::uint32_t decode_j1939_pgn(
    const std::uint32_t identifier) noexcept {
  const auto pf =
      static_cast<std::uint8_t>(
          (identifier >> 16U) & 0xFFU);
  const auto dp =
      static_cast<std::uint8_t>(
          (identifier >> 24U) & 0x01U);
  const auto ps =
      static_cast<std::uint8_t>(
          (identifier >> 8U) & 0xFFU);

  if (pf < 240U) {
    return
        (static_cast<std::uint32_t>(dp) << 16U) |
        (static_cast<std::uint32_t>(pf) << 8U);
  }

  return
      (static_cast<std::uint32_t>(dp) << 16U) |
      (static_cast<std::uint32_t>(pf) << 8U) |
      static_cast<std::uint32_t>(ps);
}

bool decode_pressure_broadcast(
    const core::transport::CanFrame& frame,
    SacPressureState& state) noexcept {
  if (frame.identifier_format !=
          core::transport::CanIdentifierFormat::extended_29_bit ||
      frame.type !=
          core::transport::CanFrameType::data ||
      frame.format !=
          core::transport::CanFrameFormat::classic ||
      frame.length < 4U) {
    return false;
  }

  const auto pf =
      static_cast<std::uint8_t>(
          (frame.identifier >> 16U) & 0xFFU);

  if (pf == 0xDAU ||
      decode_j1939_pgn(frame.identifier) !=
          kPgnPressures) {
    return false;
  }

  state.pressure1_bar =
      static_cast<float>(
          byte_value(frame.payload[2])) *
      0.08F;

  state.pressure2_bar =
      static_cast<float>(
          byte_value(frame.payload[3])) *
      0.08F;

  state.valid = true;
  return true;
}

bool decode_voltage_did_response(
    const core::protocol::uds::UdsResponse& response,
    SacVoltageState& state) noexcept {
  if (response.status !=
          core::protocol::uds::UdsStatus::ok ||
      response.length < 11U ||
      byte_value(response.payload[0]) != 0x62U ||
      byte_value(response.payload[1]) != 0xFEU ||
      byte_value(response.payload[2]) != 0x96U) {
    return false;
  }

  const auto permanent_raw =
      static_cast<std::uint16_t>(
          (static_cast<std::uint16_t>(
               byte_value(response.payload[7]))
           << 8U) |
          byte_value(response.payload[8]));

  const auto ignition_raw =
      static_cast<std::uint16_t>(
          (static_cast<std::uint16_t>(
               byte_value(response.payload[9]))
           << 8U) |
          byte_value(response.payload[10]));

  state.permanent_v =
      static_cast<float>(permanent_raw) / 10.0F;
  state.ignition_v =
      static_cast<float>(ignition_raw) / 10.0F;
  state.valid = true;
  return true;
}

}  // namespace ecu::sac
