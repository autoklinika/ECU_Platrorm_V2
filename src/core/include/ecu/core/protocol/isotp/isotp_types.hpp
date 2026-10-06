#pragma once

#include "ecu/core/time/monotonic_clock.hpp"
#include "ecu/core/transport/can_types.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ecu::core::protocol::isotp {

constexpr std::size_t kMaxPayloadSize = 4095U;

enum class IsoTpStatus : std::uint8_t {
  ok,
  in_progress,
  idle,
  busy,
  would_block,
  invalid_argument,
  payload_too_large,
  timeout,
  sequence_error,
  flow_control_overflow,
  protocol_error,
  transport_error,
  bus_off,
};

struct IsoTpAddress {
  std::uint32_t tx_id{0};
  std::uint32_t rx_id{0};
  transport::CanIdentifierFormat identifier_format{
      transport::CanIdentifierFormat::standard_11_bit};
};

struct IsoTpConfig {
  transport::CanFrameFormat frame_format{
      transport::CanFrameFormat::classic};
  std::uint8_t tx_data_length{8U};
  bool bit_rate_switch{false};

  std::uint8_t rx_block_size{0U};
  std::uint8_t rx_stmin{0U};
  std::uint8_t max_wait_frames{3U};

  std::chrono::milliseconds flow_control_timeout{1000};
  std::chrono::milliseconds consecutive_frame_timeout{1000};
};

struct IsoTpReceiveResult {
  IsoTpStatus status{IsoTpStatus::idle};
  std::size_t length{0U};
  std::array<std::byte, kMaxPayloadSize> payload{};
};

[[nodiscard]] bool is_valid_isotp_address(
    const IsoTpAddress& address) noexcept;

[[nodiscard]] bool is_valid_isotp_config(
    const IsoTpConfig& config) noexcept;

[[nodiscard]] std::chrono::nanoseconds decode_stmin(
    std::uint8_t encoded,
    bool& valid) noexcept;

}  // namespace ecu::core::protocol::isotp
