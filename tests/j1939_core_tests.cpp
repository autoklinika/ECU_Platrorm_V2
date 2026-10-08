#include "ecu/core/protocol/j1939/j1939_identifier.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace ecu::core::protocol::j1939;

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int failures = 0;

  {
    J1939IdentifierFields fields{};
    failures += require(
        decode_identifier(0x18FEAE30U, fields),
        "decode PDU2 identifier");
    failures += require(
        fields.priority == 6U &&
            fields.pdu_format == 0xFEU &&
            fields.pdu_specific == 0xAEU &&
            fields.source_address == 0x30U,
        "PDU2 fields");
    failures += require(
        !is_pdu1(fields),
        "PDU2 classification");
    failures += require(
        parameter_group_number(fields) == 0xFEAEU,
        "PDU2 PGN includes group extension");

    std::uint8_t destination = 0U;
    failures += require(
        !destination_address(fields, destination),
        "PDU2 has no destination field");
  }

  {
    J1939IdentifierFields fields{};
    failures += require(
        decode_identifier(0x18DA30F9U, fields),
        "decode PDU1 identifier");
    failures += require(
        is_pdu1(fields),
        "PDU1 classification");
    failures += require(
        parameter_group_number(fields) == 0xDA00U,
        "PDU1 PGN excludes destination address");

    std::uint8_t destination = 0U;
    failures += require(
        destination_address(fields, destination) &&
            destination == 0x30U &&
            fields.source_address == 0xF9U,
        "PDU1 destination/source addresses");
  }

  {
    std::uint32_t identifier = 0U;

    failures += require(
        encode_identifier(
            J1939MessageAddress{
                6U,
                0xDA00U,
                0xF9U,
                0x30U},
            identifier) &&
            identifier == 0x18DA30F9U,
        "encode PDU1 identifier");

    failures += require(
        encode_identifier(
            J1939MessageAddress{
                6U,
                0xFEAEU,
                0x30U,
                kGlobalAddress},
            identifier) &&
            identifier == 0x18FEAE30U,
        "encode PDU2 identifier");

    failures += require(
        !encode_identifier(
            J1939MessageAddress{
                8U,
                0xFEAEU,
                0x30U,
                kGlobalAddress},
            identifier),
        "priority above 7 rejected");

    failures += require(
        !encode_identifier(
            J1939MessageAddress{
                6U,
                0xDA01U,
                0xF9U,
                0x30U},
            identifier),
        "PDU1 PGN low byte must be zero");

    failures += require(
        !encode_identifier(
            J1939MessageAddress{
                6U,
                0xFEAEU,
                0x30U,
                0x30U},
            identifier),
        "PDU2 destination ambiguity rejected");
  }

  {
    J1939IdentifierFields fields{};
    failures += require(
        !decode_identifier(0x20000000U, fields),
        "identifier above 29 bits rejected");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "J1939_CORE_TESTS=PASS\n";
  return 0;
}
