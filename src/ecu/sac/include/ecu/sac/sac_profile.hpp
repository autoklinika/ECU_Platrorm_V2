#pragma once

#include "ecu/core/protocol/isotp/isotp_types.hpp"

#include <array>
#include <cstdint>

namespace ecu::sac {

constexpr std::uint32_t kRequestCanId = 0x18DA30F9U;
constexpr std::uint32_t kResponseCanId = 0x18DAF930U;

constexpr std::uint8_t kEcuSourceAddress = 0x30U;
constexpr std::uint8_t kTesterSourceAddress = 0xF9U;

constexpr std::array<std::uint32_t, 2> kLegacyBitrateCandidates{
    250000U,
    500000U};

constexpr std::uint16_t kDidVin = 0xF190U;
constexpr std::uint16_t kDidSoftware = 0xF188U;
constexpr std::uint16_t kDidHardware = 0xF192U;
constexpr std::uint16_t kDidVoltage = 0xFE96U;

constexpr std::uint32_t kPgnPressures = 65198U;

[[nodiscard]] constexpr core::protocol::isotp::IsoTpAddress
diagnostic_address() noexcept {
  return core::protocol::isotp::IsoTpAddress{
      kRequestCanId,
      kResponseCanId,
      core::transport::CanIdentifierFormat::extended_29_bit};
}

}  // namespace ecu::sac
