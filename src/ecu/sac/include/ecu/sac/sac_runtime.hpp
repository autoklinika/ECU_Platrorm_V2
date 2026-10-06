#pragma once

#include "ecu/core/protocol/uds/uds_types.hpp"
#include "ecu/core/transport/can_types.hpp"

#include <cstdint>

namespace ecu::sac {

struct SacPressureState {
  bool valid{false};
  float pressure1_bar{0.0F};
  float pressure2_bar{0.0F};
};

struct SacVoltageState {
  bool valid{false};
  float permanent_v{0.0F};
  float ignition_v{0.0F};
};

[[nodiscard]] std::uint32_t decode_j1939_pgn(
    std::uint32_t identifier) noexcept;

[[nodiscard]] bool decode_pressure_broadcast(
    const core::transport::CanFrame& frame,
    SacPressureState& state) noexcept;

[[nodiscard]] bool decode_voltage_did_response(
    const core::protocol::uds::UdsResponse& response,
    SacVoltageState& state) noexcept;

}  // namespace ecu::sac
