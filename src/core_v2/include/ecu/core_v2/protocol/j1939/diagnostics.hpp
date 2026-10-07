#pragma once

#include "ecu/core_v2/protocol/j1939/j1939_identifier.hpp"
#include "ecu/core_v2/protocol/j1939/transport_protocol.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::core::v2::protocol::j1939 {

inline constexpr std::uint32_t kDm1Pgn = 0xFECAU;
inline constexpr std::uint32_t kDm2Pgn = 0xFECBU;
inline constexpr std::size_t kDtcEncodedBytes = 4U;

enum class DiagnosticLampStatus : std::uint8_t {
  off = 0U,
  on = 1U,
  error = 2U,
  not_available = 3U,
};

enum class DiagnosticLampFlash : std::uint8_t {
  slow = 0U,
  fast = 1U,
  reserved = 2U,
  not_available = 3U,
};

struct DiagnosticLampState {
  DiagnosticLampStatus malfunction_indicator{
      DiagnosticLampStatus::not_available};
  DiagnosticLampStatus red_stop{
      DiagnosticLampStatus::not_available};
  DiagnosticLampStatus amber_warning{
      DiagnosticLampStatus::not_available};
  DiagnosticLampStatus protect{
      DiagnosticLampStatus::not_available};

  DiagnosticLampFlash malfunction_indicator_flash{
      DiagnosticLampFlash::not_available};
  DiagnosticLampFlash red_stop_flash{
      DiagnosticLampFlash::not_available};
  DiagnosticLampFlash amber_warning_flash{
      DiagnosticLampFlash::not_available};
  DiagnosticLampFlash protect_flash{
      DiagnosticLampFlash::not_available};
};

struct DiagnosticTroubleCode {
  std::uint32_t spn{0U};
  std::uint8_t fmi{0U};
  std::uint8_t occurrence_count{0U};
  bool conversion_method{false};
};

enum class DiagnosticDecodeStatus : std::uint8_t {
  ok,
  invalid_argument,
  unsupported_conversion_method,
};

struct DiagnosticMessageInfo {
  std::uint32_t pgn{0U};
  std::uint8_t source_address{kNullAddress};
  DiagnosticLampState lamps{};
  std::size_t dtc_count{0U};
  bool transported{false};
};

[[nodiscard]] constexpr bool is_dm1_or_dm2_pgn(
    const std::uint32_t pgn) noexcept {
  return pgn == kDm1Pgn || pgn == kDm2Pgn;
}

[[nodiscard]] bool decode_diagnostic_lamps(
    std::byte lamp_status,
    std::byte lamp_flash,
    DiagnosticLampState& state) noexcept;

[[nodiscard]] bool decode_dm_frame(
    const transport::CanFrame& frame,
    DiagnosticMessageInfo& info) noexcept;

[[nodiscard]] bool decode_dm_transport(
    const TpMessage& message,
    DiagnosticMessageInfo& info) noexcept;

[[nodiscard]] DiagnosticDecodeStatus decode_dtc(
    const transport::CanFrame& frame,
    std::size_t index,
    DiagnosticTroubleCode& dtc) noexcept;

[[nodiscard]] DiagnosticDecodeStatus decode_dtc(
    const TpMessage& message,
    std::size_t index,
    DiagnosticTroubleCode& dtc) noexcept;

}  // namespace ecu::core::v2::protocol::j1939
