#pragma once

#include "ecu/core/time/monotonic_clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::core::transport {

enum class CanIdentifierFormat : std::uint8_t {
  standard_11_bit,
  extended_29_bit,
};

enum class CanFrameFormat : std::uint8_t {
  classic,
  fd,
};

enum class CanFrameType : std::uint8_t {
  data,
  remote,
};

struct CanFrame {
  std::uint32_t identifier{0};
  CanIdentifierFormat identifier_format{CanIdentifierFormat::standard_11_bit};
  CanFrameFormat format{CanFrameFormat::classic};
  CanFrameType type{CanFrameType::data};
  bool bit_rate_switch{false};
  bool error_state_indicator{false};
  std::uint8_t length{0};
  std::array<std::byte, 64> payload{};
};

struct ReceivedCanFrame {
  CanFrame frame{};
  time::MonotonicTime timestamp{};
};

struct CanCapabilities {
  bool classic_can{true};
  bool can_fd{false};
  bool bit_rate_switch{false};
  bool listen_only{false};
  std::uint8_t max_payload_bytes{8};
};

enum class CanMode : std::uint8_t {
  normal,
  listen_only,
};

struct CanChannelConfig {
  std::uint32_t nominal_bitrate{0};
  bool fd_enabled{false};
  std::uint32_t data_bitrate{0};
  CanMode mode{CanMode::normal};
};

enum class CanStatus : std::uint8_t {
  ok,
  would_block,
  not_open,
  invalid_argument,
  unsupported,
  bus_off,
  io_error,
};

struct CanReceiveResult {
  CanStatus status{CanStatus::would_block};
  ReceivedCanFrame value{};
};

[[nodiscard]] bool is_valid_can_frame(const CanFrame& frame) noexcept;
[[nodiscard]] bool is_valid_can_channel_config(
    const CanChannelConfig& config) noexcept;
[[nodiscard]] bool capabilities_support(
    const CanCapabilities& capabilities,
    const CanChannelConfig& config) noexcept;
[[nodiscard]] bool capabilities_support_frame(
    const CanCapabilities& capabilities,
    const CanFrame& frame) noexcept;

}  // namespace ecu::core::transport
