#pragma once

#include "ecu/core_v2/protocol/j1939/request.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::core::v2::protocol::j1939 {

inline constexpr std::uint32_t kFdMultiPgPgn = 0x2500U;
inline constexpr std::size_t kContainedPgHeaderBytes = 4U;
inline constexpr std::uint8_t kContainedPgMaxPayloadBytes = 60U;
inline constexpr std::uint8_t kContainedPgTosPadding = 0U;
inline constexpr std::uint8_t kContainedPgTosSaeNoAssurance = 2U;
inline constexpr std::uint8_t kContainedPgTrailerNone = 0U;

struct ContainedPgHeader {
  std::uint8_t type_of_service{kContainedPgTosSaeNoAssurance};
  std::uint8_t trailer_format{kContainedPgTrailerNone};
  std::uint32_t pgn{0U};
  std::uint8_t payload_length{0U};
};

enum class ContainedPgDecodeStatus : std::uint8_t {
  ok,
  padding,
  invalid_argument,
  unsupported_profile,
};

struct ExtendedMultiPgEnvelope {
  std::uint8_t priority{0U};
  std::uint8_t source_address{kNullAddress};
  std::uint8_t destination_address{kGlobalAddress};
};

struct ContainedPgView {
  std::uint32_t pgn{0U};
  std::uint8_t payload_length{0U};
  std::size_t payload_offset{0U};
};

[[nodiscard]] bool encode_no_assurance_contained_pg_header(
    std::uint32_t pgn,
    std::uint8_t payload_length,
    std::array<std::byte, kContainedPgHeaderBytes>& header) noexcept;

[[nodiscard]] ContainedPgDecodeStatus
decode_no_assurance_contained_pg_header(
    const std::array<std::byte, kContainedPgHeaderBytes>& header,
    ContainedPgHeader& decoded) noexcept;

[[nodiscard]] bool decode_extended_multi_pg_envelope(
    const transport::CanFrame& frame,
    ExtendedMultiPgEnvelope& envelope) noexcept;

[[nodiscard]] ContainedPgDecodeStatus decode_contained_pg_at(
    const transport::CanFrame& frame,
    std::size_t offset,
    ContainedPgView& view,
    std::size_t& next_offset) noexcept;

class ExtendedMultiPgBuilder final {
 public:
  [[nodiscard]] bool begin(
      std::uint8_t priority,
      std::uint8_t source_address,
      std::uint8_t destination_address) noexcept;

  [[nodiscard]] bool append(
      std::uint32_t pgn,
      const std::byte* payload,
      std::uint8_t payload_length) noexcept;

  [[nodiscard]] bool finalize(
      transport::CanFrame& frame) noexcept;

  [[nodiscard]] std::size_t used_payload_bytes() const noexcept {
    return used_;
  }

  [[nodiscard]] bool active() const noexcept {
    return active_;
  }

 private:
  transport::CanFrame frame_{};
  std::size_t used_{0U};
  bool active_{false};
};

}  // namespace ecu::core::v2::protocol::j1939
