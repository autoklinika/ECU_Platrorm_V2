#pragma once

#include <cstdint>

namespace ecu::core::protocol::j1939 {

constexpr std::uint8_t kGlobalAddress = 0xFFU;
constexpr std::uint32_t kMaxPgn = 0x3FFFFU;
constexpr std::uint32_t kMaxCanIdentifier = 0x1FFFFFFFU;

struct J1939IdentifierFields {
  std::uint8_t priority{0U};
  bool reserved{false};
  bool data_page{false};
  std::uint8_t pdu_format{0U};
  std::uint8_t pdu_specific{0U};
  std::uint8_t source_address{0U};
};

struct J1939MessageAddress {
  std::uint8_t priority{6U};
  std::uint32_t pgn{0U};
  std::uint8_t source_address{0U};
  std::uint8_t destination_address{kGlobalAddress};
};

[[nodiscard]] bool decode_identifier(
    std::uint32_t identifier,
    J1939IdentifierFields& fields) noexcept;

[[nodiscard]] constexpr bool is_pdu1(
    const J1939IdentifierFields& fields) noexcept {
  return fields.pdu_format < 240U;
}

[[nodiscard]] std::uint32_t parameter_group_number(
    const J1939IdentifierFields& fields) noexcept;

[[nodiscard]] bool destination_address(
    const J1939IdentifierFields& fields,
    std::uint8_t& destination) noexcept;

[[nodiscard]] bool encode_identifier(
    const J1939MessageAddress& address,
    std::uint32_t& identifier) noexcept;

}  // namespace ecu::core::protocol::j1939
