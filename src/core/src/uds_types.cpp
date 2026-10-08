#include "ecu/core/protocol/uds/uds_types.hpp"

namespace ecu::core::protocol::uds {

bool is_valid_uds_timing(const UdsTiming& timing) noexcept {
  return timing.p2.count() > 0 &&
         timing.p2_star.count() > 0 &&
         timing.p2_star >= timing.p2;
}

std::uint8_t positive_response_sid(
    const std::uint8_t request_sid) noexcept {
  return static_cast<std::uint8_t>(request_sid + 0x40U);
}

}  // namespace ecu::core::protocol::uds
