#pragma once

#include "ecu/core/protocol/uds/uds_client.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::sac {

struct SacDtcRecord {
  std::uint32_t code{0U};
  std::uint8_t status{0U};
};

enum class SacDtcReadStatus : std::uint8_t {
  idle,
  in_progress,
  done,
  busy,
  uds_error,
  invalid_response,
  capacity_exceeded,
};

struct SacDtcReadResult {
  std::uint8_t status_availability_mask{0U};
  std::array<SacDtcRecord, 128> records{};
  std::size_t count{0U};
};

class SacDtcReader {
 public:
  explicit SacDtcReader(
      core::protocol::uds::UdsClient& uds) noexcept;

  SacDtcReadStatus start(
      std::uint8_t status_mask = 0xFFU) noexcept;

  SacDtcReadStatus poll() noexcept;

  [[nodiscard]] SacDtcReadStatus status() const noexcept;
  [[nodiscard]] const SacDtcReadResult& result() const noexcept;
  [[nodiscard]] std::uint8_t last_nrc() const noexcept;

  void reset() noexcept;

 private:
  enum class Step : std::uint8_t {
    idle,
    request_session,
    wait_session,
    request_dtc,
    wait_dtc,
    done,
    error,
  };

  core::protocol::uds::UdsClient& uds_;
  Step step_{Step::idle};
  SacDtcReadStatus status_{SacDtcReadStatus::idle};
  SacDtcReadResult result_{};
  std::uint8_t requested_status_mask_{0xFFU};
  std::uint8_t last_nrc_{0U};

  [[nodiscard]] SacDtcReadStatus handle_session_response() noexcept;
  [[nodiscard]] SacDtcReadStatus handle_dtc_response() noexcept;

  void fail(
      SacDtcReadStatus status,
      std::uint8_t nrc = 0U) noexcept;
};

}  // namespace ecu::sac
