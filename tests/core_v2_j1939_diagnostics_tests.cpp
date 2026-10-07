#include "ecu/core_v2/protocol/j1939/diagnostics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using namespace ecu::core::v2;
using namespace ecu::core::v2::protocol::j1939;

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

std::array<std::uint8_t, 4U> encoded_dtc(
    const std::uint32_t spn,
    const std::uint8_t fmi,
    const std::uint8_t occurrence_count,
    const bool conversion_method = false) {
  return {
      static_cast<std::uint8_t>(spn & 0xFFU),
      static_cast<std::uint8_t>((spn >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(
          ((spn >> 16U) & 0x07U) << 5U |
          (static_cast<std::uint32_t>(fmi) & 0x1FU)),
      static_cast<std::uint8_t>(
          (conversion_method ? 0x80U : 0U) |
          (occurrence_count & 0x7FU))};
}

transport::CanFrame single_dm(
    const std::uint32_t pgn,
    const std::uint8_t source_address,
    const bool with_dtc,
    const bool conversion_method = false) {
  std::uint8_t payload[8] = {
      0x47U,
      0x73U,
      0U,
      0U,
      0U,
      0U,
      0xFFU,
      0xFFU};

  if (with_dtc) {
    const auto dtc =
        encoded_dtc(0x52345U, 5U, 12U, conversion_method);
    for (std::size_t i = 0U; i < dtc.size(); ++i) {
      payload[2U + i] = dtc[i];
    }
  }

  transport::CanFrame frame{};
  (void)build_classic_data_frame(
      MessageAddress{
          6U,
          pgn,
          source_address,
          kGlobalAddress},
      payload,
      8U,
      frame);
  return frame;
}

TpMessage transported_dm(const std::uint32_t pgn) {
  TpMessage message{};
  message.pgn = pgn;
  message.source_address = 0x80U;
  message.destination_address = kGlobalAddress;
  message.size = 10U;
  message.broadcast = true;
  message.data[0U] = std::byte{0x47U};
  message.data[1U] = std::byte{0x73U};

  const auto first = encoded_dtc(0x52345U, 5U, 12U);
  const auto second = encoded_dtc(0x10203U, 7U, 2U);
  for (std::size_t i = 0U; i < first.size(); ++i) {
    message.data[2U + i] = static_cast<std::byte>(first[i]);
    message.data[6U + i] = static_cast<std::byte>(second[i]);
  }
  return message;
}

}  // namespace

int main() {
  int failures = 0;

  {
    const auto frame = single_dm(kDm1Pgn, 0x80U, false);
    DiagnosticMessageInfo info{};
    failures += require(
        decode_dm_frame(frame, info) &&
            info.pgn == kDm1Pgn &&
            info.source_address == 0x80U &&
            !info.transported &&
            info.dtc_count == 0U,
        "single-frame DM1 without DTC decodes");

    failures += require(
        info.lamps.malfunction_indicator ==
                DiagnosticLampStatus::on &&
            info.lamps.red_stop ==
                DiagnosticLampStatus::off &&
            info.lamps.amber_warning ==
                DiagnosticLampStatus::on &&
            info.lamps.protect ==
                DiagnosticLampStatus::not_available &&
            info.lamps.malfunction_indicator_flash ==
                DiagnosticLampFlash::fast &&
            info.lamps.red_stop_flash ==
                DiagnosticLampFlash::not_available &&
            info.lamps.amber_warning_flash ==
                DiagnosticLampFlash::slow &&
            info.lamps.protect_flash ==
                DiagnosticLampFlash::not_available,
        "DM lamp and flash bit pairs decode");

    DiagnosticTroubleCode dtc{};
    failures += require(
        decode_dtc(frame, 0U, dtc) ==
            DiagnosticDecodeStatus::invalid_argument,
        "zero-DTC DM1 has no decodable DTC");
  }

  {
    const auto frame = single_dm(kDm1Pgn, 0x80U, true);
    DiagnosticMessageInfo info{};
    failures += require(
        decode_dm_frame(frame, info) &&
            info.dtc_count == 1U,
        "single-frame DM1 with one DTC decodes");

    DiagnosticTroubleCode dtc{};
    failures += require(
        decode_dtc(frame, 0U, dtc) ==
                DiagnosticDecodeStatus::ok &&
            dtc.spn == 0x52345U &&
            dtc.fmi == 5U &&
            dtc.occurrence_count == 12U &&
            !dtc.conversion_method,
        "current-method DTC fields decode");
    failures += require(
        decode_dtc(frame, 1U, dtc) ==
            DiagnosticDecodeStatus::invalid_argument,
        "DTC index is bounded");
  }

  {
    const auto legacy = single_dm(kDm1Pgn, 0x80U, true, true);
    DiagnosticTroubleCode dtc{};
    failures += require(
        decode_dtc(legacy, 0U, dtc) ==
                DiagnosticDecodeStatus::
                    unsupported_conversion_method &&
            dtc.spn == 0U &&
            dtc.fmi == 5U &&
            dtc.occurrence_count == 12U &&
            dtc.conversion_method,
        "legacy conversion method fails closed without inventing SPN");
  }

  {
    auto malformed = single_dm(kDm1Pgn, 0x80U, true);
    malformed.length = 7U;
    DiagnosticMessageInfo info{};
    failures += require(
        !decode_dm_frame(malformed, info),
        "single-frame DM requires exact DLC 8");

    malformed = single_dm(kDm1Pgn, 0x80U, true);
    malformed.payload[7U] = std::byte{0U};
    failures += require(
        !decode_dm_frame(malformed, info),
        "single-frame DM filler bytes are validated");

    malformed = single_dm(kDm1Pgn, 0x80U, true);
    malformed.identifier =
        (malformed.identifier & 0x1FFFFF00U) |
        kGlobalAddress;
    failures += require(
        !decode_dm_frame(malformed, info),
        "global source address is rejected for DM");
  }

  {
    const auto message = transported_dm(kDm1Pgn);
    DiagnosticMessageInfo info{};
    failures += require(
        decode_dm_transport(message, info) &&
            info.pgn == kDm1Pgn &&
            info.source_address == 0x80U &&
            info.transported &&
            info.dtc_count == 2U,
        "transported DM1 with two DTCs decodes");

    DiagnosticTroubleCode first{};
    DiagnosticTroubleCode second{};
    failures += require(
        decode_dtc(message, 0U, first) ==
                DiagnosticDecodeStatus::ok &&
            first.spn == 0x52345U &&
            first.fmi == 5U &&
            first.occurrence_count == 12U &&
            decode_dtc(message, 1U, second) ==
                DiagnosticDecodeStatus::ok &&
            second.spn == 0x10203U &&
            second.fmi == 7U &&
            second.occurrence_count == 2U,
        "transported DTC records decode independently");
  }

  {
    const auto message = transported_dm(kDm2Pgn);
    DiagnosticMessageInfo info{};
    failures += require(
        decode_dm_transport(message, info) &&
            info.pgn == kDm2Pgn &&
            info.dtc_count == 2U,
        "transported DM2 uses the same bounded DTC representation");
  }

  {
    auto malformed = transported_dm(kDm1Pgn);
    DiagnosticMessageInfo info{};

    malformed.size = 9U;
    failures += require(
        !decode_dm_transport(malformed, info),
        "transported DM size must be 2 plus whole DTC records");

    malformed = transported_dm(kDm1Pgn);
    malformed.broadcast = false;
    malformed.destination_address = 0x90U;
    failures += require(
        !decode_dm_transport(malformed, info),
        "DM1/DM2 transport must be broadcast/global");

    malformed = transported_dm(0xFEC0U);
    failures += require(
        !decode_dm_transport(malformed, info),
        "unrelated transported PGN is rejected");
  }

  failures += require(
      sizeof(DiagnosticMessageInfo) <= 64U,
      "diagnostic message metadata remains small and bounded");

  if (failures == 0) {
    std::cout << "CORE_V2_J1939_DIAGNOSTICS_TESTS=PASS\n";
  }
  return failures == 0 ? 0 : 1;
}
