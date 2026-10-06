#pragma once

#include "ecu/core/protocol/isotp/isotp_types.hpp"
#include "ecu/core/time/monotonic_clock.hpp"
#include "ecu/core/transport/i_can_interface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::core::protocol::isotp {

class IsoTpEndpoint {
 public:
  IsoTpEndpoint(
      transport::ICanInterface& can,
      const time::IMonotonicClock& clock,
      IsoTpAddress address,
      IsoTpConfig config) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  IsoTpStatus start_send(
      const std::byte* payload,
      std::size_t length) noexcept;

  IsoTpStatus poll() noexcept;

  [[nodiscard]] bool tx_busy() const noexcept;
  [[nodiscard]] IsoTpStatus last_tx_status() const noexcept;

  [[nodiscard]] bool has_received() const noexcept;
  [[nodiscard]] IsoTpReceiveResult take_received() noexcept;

  void reset() noexcept;

 private:
  enum class TxState : std::uint8_t {
    idle,
    single_pending,
    first_pending,
    waiting_flow_control,
    sending_consecutive,
  };

  transport::ICanInterface& can_;
  const time::IMonotonicClock& clock_;
  IsoTpAddress address_;
  IsoTpConfig config_;
  bool valid_{false};

  TxState tx_state_{TxState::idle};
  IsoTpStatus last_tx_status_{IsoTpStatus::idle};
  std::array<std::byte, kMaxPayloadSize> tx_payload_{};
  std::size_t tx_length_{0U};
  std::size_t tx_offset_{0U};
  std::uint8_t tx_sequence_{1U};
  std::uint8_t tx_block_size_{0U};
  std::uint8_t tx_block_sent_{0U};
  std::uint8_t tx_wait_frames_{0U};
  std::chrono::nanoseconds tx_stmin_{0};
  time::MonotonicTime tx_next_send_{0};
  time::MonotonicTime tx_deadline_{0};

  bool rx_active_{false};
  bool rx_complete_{false};
  std::array<std::byte, kMaxPayloadSize> rx_payload_{};
  std::size_t rx_length_{0U};
  std::size_t rx_offset_{0U};
  std::uint8_t rx_expected_sequence_{1U};
  std::uint8_t rx_block_received_{0U};
  time::MonotonicTime rx_deadline_{0};

  bool pending_control_{false};
  transport::CanFrame control_frame_{};

  [[nodiscard]] IsoTpStatus process_incoming(
      const transport::ReceivedCanFrame& received,
      time::MonotonicTime now) noexcept;

  [[nodiscard]] IsoTpStatus process_flow_control(
      const transport::CanFrame& frame,
      time::MonotonicTime now) noexcept;

  [[nodiscard]] IsoTpStatus process_single_frame(
      const transport::CanFrame& frame) noexcept;

  [[nodiscard]] IsoTpStatus process_first_frame(
      const transport::CanFrame& frame,
      time::MonotonicTime now) noexcept;

  [[nodiscard]] IsoTpStatus process_consecutive_frame(
      const transport::CanFrame& frame,
      time::MonotonicTime now) noexcept;

  [[nodiscard]] IsoTpStatus poll_tx(
      time::MonotonicTime now) noexcept;

  [[nodiscard]] IsoTpStatus send_pending_control() noexcept;

  [[nodiscard]] IsoTpStatus queue_flow_control(
      std::uint8_t flow_status) noexcept;

  [[nodiscard]] transport::CanFrame make_base_tx_frame() const noexcept;

  [[nodiscard]] std::uint8_t choose_wire_length(
      std::uint8_t used_bytes) const noexcept;

  [[nodiscard]] std::size_t single_frame_capacity() const noexcept;

  [[nodiscard]] IsoTpStatus map_can_status(
      transport::CanStatus status) const noexcept;

  void fail_tx(IsoTpStatus status) noexcept;
  void reset_rx_transfer() noexcept;
};

}  // namespace ecu::core::protocol::isotp
