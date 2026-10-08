#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ecu::core::protocol::uds {

constexpr std::size_t kMaxUdsPayloadSize = 4095U;

enum class UdsStatus : std::uint8_t {
  ok,
  in_progress,
  idle,
  busy,
  invalid_argument,
  payload_too_large,
  timeout_p2,
  timeout_p2_star,
  transport_error,
  protocol_error,
  negative_response,
};

enum class UdsNegativeResponseCode : std::uint8_t {
  general_reject = 0x10U,
  service_not_supported = 0x11U,
  sub_function_not_supported = 0x12U,
  incorrect_message_length_or_invalid_format = 0x13U,
  response_too_long = 0x14U,
  busy_repeat_request = 0x21U,
  conditions_not_correct = 0x22U,
  request_sequence_error = 0x24U,
  request_out_of_range = 0x31U,
  security_access_denied = 0x33U,
  invalid_key = 0x35U,
  exceed_number_of_attempts = 0x36U,
  required_time_delay_not_expired = 0x37U,
  upload_download_not_accepted = 0x70U,
  transfer_data_suspended = 0x71U,
  general_programming_failure = 0x72U,
  wrong_block_sequence_counter = 0x73U,
  response_pending = 0x78U,
  sub_function_not_supported_in_active_session = 0x7EU,
  service_not_supported_in_active_session = 0x7FU,
};

struct UdsTiming {
  std::chrono::milliseconds p2{50};
  std::chrono::milliseconds p2_star{5000};
};

struct UdsResponse {
  UdsStatus status{UdsStatus::idle};
  std::uint8_t request_sid{0U};
  std::uint8_t response_sid{0U};
  std::uint8_t negative_response_code{0U};
  std::size_t length{0U};
  std::array<std::byte, kMaxUdsPayloadSize> payload{};
};

struct UdsRequest {
  std::size_t length{0U};
  std::array<std::byte, kMaxUdsPayloadSize> payload{};
};

[[nodiscard]] bool is_valid_uds_timing(
    const UdsTiming& timing) noexcept;

[[nodiscard]] std::uint8_t positive_response_sid(
    std::uint8_t request_sid) noexcept;

}  // namespace ecu::core::protocol::uds
