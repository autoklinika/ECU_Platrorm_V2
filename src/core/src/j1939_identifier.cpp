#include "ecu/core/protocol/j1939/j1939_identifier.hpp"

namespace ecu::core::protocol::j1939 {

bool decode_identifier(
    const std::uint32_t identifier,
    J1939IdentifierFields& fields) noexcept {
  if (identifier > kMaxCanIdentifier) {
    return false;
  }

  fields.priority =
      static_cast<std::uint8_t>((identifier >> 26U) & 0x07U);
  fields.reserved = ((identifier >> 25U) & 0x01U) != 0U;
  fields.data_page = ((identifier >> 24U) & 0x01U) != 0U;
  fields.pdu_format =
      static_cast<std::uint8_t>((identifier >> 16U) & 0xFFU);
  fields.pdu_specific =
      static_cast<std::uint8_t>((identifier >> 8U) & 0xFFU);
  fields.source_address =
      static_cast<std::uint8_t>(identifier & 0xFFU);

  return true;
}

std::uint32_t parameter_group_number(
    const J1939IdentifierFields& fields) noexcept {
  std::uint32_t pgn =
      (static_cast<std::uint32_t>(fields.reserved) << 17U) |
      (static_cast<std::uint32_t>(fields.data_page) << 16U) |
      (static_cast<std::uint32_t>(fields.pdu_format) << 8U);

  if (!is_pdu1(fields)) {
    pgn |= static_cast<std::uint32_t>(fields.pdu_specific);
  }

  return pgn;
}

bool destination_address(
    const J1939IdentifierFields& fields,
    std::uint8_t& destination) noexcept {
  if (!is_pdu1(fields)) {
    return false;
  }

  destination = fields.pdu_specific;
  return true;
}

bool encode_identifier(
    const J1939MessageAddress& address,
    std::uint32_t& identifier) noexcept {
  if (address.priority > 7U ||
      address.pgn > kMaxPgn) {
    return false;
  }

  const auto pdu_format =
      static_cast<std::uint8_t>((address.pgn >> 8U) & 0xFFU);

  std::uint32_t mapped_pgn = address.pgn;

  if (pdu_format < 240U) {
    if ((address.pgn & 0xFFU) != 0U) {
      return false;
    }

    mapped_pgn |=
        static_cast<std::uint32_t>(address.destination_address);
  } else if (address.destination_address != kGlobalAddress) {
    return false;
  }

  identifier =
      (static_cast<std::uint32_t>(address.priority) << 26U) |
      (mapped_pgn << 8U) |
      static_cast<std::uint32_t>(address.source_address);

  return identifier <= kMaxCanIdentifier;
}

}  // namespace ecu::core::protocol::j1939
