#pragma once

#include "ecu/core/protocol/uds/uds_client.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ecu::sac {

enum class SacIdentificationStatus : std::uint8_t {
  idle,
  in_progress,
  done,
  busy,
  uds_error,
  invalid_response,
};

struct SacTextField {
  static constexpr std::size_t kCapacity = 64U;

  std::array<char, kCapacity> data{};
  std::size_t length{0U};

  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view{data.data(), length};
  }
};

struct SacIdentificationResult {
  SacTextField vin{};
  SacTextField software{};
  SacTextField hardware{};
};

class SacIdentification {
 public:
  explicit SacIdentification(
      core::protocol::uds::UdsClient& uds) noexcept;

  SacIdentificationStatus start() noexcept;
  SacIdentificationStatus poll() noexcept;

  [[nodiscard]] SacIdentificationStatus status() const noexcept;
  [[nodiscard]] const SacIdentificationResult& result() const noexcept;
  [[nodiscard]] std::uint8_t last_nrc() const noexcept;

  void reset() noexcept;

 private:
  enum class Step : std::uint8_t {
    idle,
    request_vin,
    wait_vin,
    request_software,
    wait_software,
    request_hardware,
    wait_hardware,
    done,
    error,
  };

  core::protocol::uds::UdsClient& uds_;
  Step step_{Step::idle};
  SacIdentificationStatus status_{SacIdentificationStatus::idle};
  SacIdentificationResult result_{};
  std::uint8_t last_nrc_{0U};

  [[nodiscard]] SacIdentificationStatus start_did(
      std::uint16_t did,
      Step wait_step) noexcept;

  [[nodiscard]] SacIdentificationStatus handle_did_response(
      std::uint16_t expected_did,
      SacTextField& target,
      Step next_step) noexcept;

  [[nodiscard]] bool parse_text_did(
      const core::protocol::uds::UdsResponse& response,
      std::uint16_t expected_did,
      SacTextField& target) noexcept;

  void fail(
      SacIdentificationStatus status,
      std::uint8_t nrc = 0U) noexcept;
};

}  // namespace ecu::sac
