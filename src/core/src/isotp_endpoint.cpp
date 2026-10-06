#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ecu::core::protocol::isotp {
namespace {

constexpr std::uint8_t kSingleFrame = 0x00U;
constexpr std::uint8_t kFirstFrame = 0x10U;
constexpr std::uint8_t kConsecutiveFrame = 0x20U;
constexpr std::uint8_t kFlowControl = 0x30U;

constexpr std::uint8_t kFlowContinueToSend = 0x00U;
constexpr std::uint8_t kFlowWait = 0x01U;
constexpr std::uint8_t kFlowOverflow = 0x02U;

constexpr std::size_t kMaxFramesPerPoll = 64U;

std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

}  // namespace

IsoTpEndpoint::IsoTpEndpoint(
    transport::ICanInterface& can,
    const time::IMonotonicClock& clock,
    const IsoTpAddress address,
    const IsoTpConfig config) noexcept
    : can_(can),
      clock_(clock),
      address_(address),
      config_(config),
      valid_(
          is_valid_isotp_address(address) &&
          is_valid_isotp_config(config)) {}

bool IsoTpEndpoint::valid() const noexcept {
  return valid_;
}

IsoTpStatus IsoTpEndpoint::start_send(
    const std::byte* payload,
    const std::size_t length) noexcept {
  if (!valid_ || payload == nullptr || length == 0U) {
    return IsoTpStatus::invalid_argument;
  }

  if (length > kMaxPayloadSize) {
    return IsoTpStatus::payload_too_large;
  }

  if (tx_state_ != TxState::idle) {
    return IsoTpStatus::busy;
  }

  std::memcpy(tx_payload_.data(), payload, length);
  tx_length_ = length;
  tx_offset_ = 0U;
  tx_sequence_ = 1U;
  tx_block_size_ = 0U;
  tx_block_sent_ = 0U;
  tx_wait_frames_ = 0U;
  tx_stmin_ = std::chrono::nanoseconds{0};
  tx_next_send_ = time::MonotonicTime{0};
  tx_deadline_ = time::MonotonicTime{0};
  last_tx_status_ = IsoTpStatus::in_progress;

  tx_state_ =
      length <= single_frame_capacity()
          ? TxState::single_pending
          : TxState::first_pending;

  return IsoTpStatus::in_progress;
}

IsoTpStatus IsoTpEndpoint::poll() noexcept {
  if (!valid_) {
    return IsoTpStatus::invalid_argument;
  }

  const auto now = clock_.now();

  if (rx_active_ && now > rx_deadline_) {
    reset_rx_transfer();
    return IsoTpStatus::timeout;
  }

  if (tx_state_ == TxState::waiting_flow_control &&
      now > tx_deadline_) {
    fail_tx(IsoTpStatus::timeout);
    return IsoTpStatus::timeout;
  }

  for (std::size_t count = 0U; count < kMaxFramesPerPoll; ++count) {
    auto result = can_.try_receive();

    if (result.status == transport::CanStatus::would_block) {
      break;
    }

    if (result.status != transport::CanStatus::ok) {
      return map_can_status(result.status);
    }

    const auto status = process_incoming(result.value, now);
    if (status != IsoTpStatus::ok &&
        status != IsoTpStatus::in_progress &&
        status != IsoTpStatus::idle) {
      return status;
    }
  }

  if (pending_control_) {
    const auto status = send_pending_control();
    if (status != IsoTpStatus::ok &&
        status != IsoTpStatus::would_block) {
      return status;
    }
  }

  const auto tx_status = poll_tx(now);
  if (tx_status != IsoTpStatus::ok &&
      tx_status != IsoTpStatus::in_progress &&
      tx_status != IsoTpStatus::idle &&
      tx_status != IsoTpStatus::would_block) {
    return tx_status;
  }

  if (tx_state_ != TxState::idle || rx_active_ || pending_control_) {
    return IsoTpStatus::in_progress;
  }

  return IsoTpStatus::ok;
}

bool IsoTpEndpoint::tx_busy() const noexcept {
  return tx_state_ != TxState::idle;
}

IsoTpStatus IsoTpEndpoint::last_tx_status() const noexcept {
  return last_tx_status_;
}

bool IsoTpEndpoint::has_received() const noexcept {
  return rx_complete_;
}

IsoTpReceiveResult IsoTpEndpoint::take_received() noexcept {
  IsoTpReceiveResult result{};

  if (!rx_complete_) {
    return result;
  }

  result.status = IsoTpStatus::ok;
  result.length = rx_length_;
  std::memcpy(
      result.payload.data(),
      rx_payload_.data(),
      rx_length_);

  rx_complete_ = false;
  rx_length_ = 0U;
  rx_offset_ = 0U;
  return result;
}

void IsoTpEndpoint::reset() noexcept {
  tx_state_ = TxState::idle;
  last_tx_status_ = IsoTpStatus::idle;
  tx_length_ = 0U;
  tx_offset_ = 0U;
  tx_sequence_ = 1U;
  tx_block_size_ = 0U;
  tx_block_sent_ = 0U;
  tx_wait_frames_ = 0U;
  tx_stmin_ = std::chrono::nanoseconds{0};
  tx_next_send_ = time::MonotonicTime{0};
  tx_deadline_ = time::MonotonicTime{0};

  rx_complete_ = false;
  rx_length_ = 0U;
  rx_offset_ = 0U;
  reset_rx_transfer();

  pending_control_ = false;
  control_frame_ = {};
}

IsoTpStatus IsoTpEndpoint::process_incoming(
    const transport::ReceivedCanFrame& received,
    const time::MonotonicTime now) noexcept {
  const auto& frame = received.frame;

  if (frame.identifier != address_.rx_id ||
      frame.identifier_format != address_.identifier_format ||
      frame.type != transport::CanFrameType::data ||
      frame.format != config_.frame_format ||
      frame.length == 0U) {
    return IsoTpStatus::ok;
  }

  const auto pci = byte_value(frame.payload[0]);
  switch (pci & 0xF0U) {
    case kSingleFrame:
      return process_single_frame(frame);
    case kFirstFrame:
      return process_first_frame(frame, now);
    case kConsecutiveFrame:
      return process_consecutive_frame(frame, now);
    case kFlowControl:
      return process_flow_control(frame, now);
    default:
      return IsoTpStatus::protocol_error;
  }
}

IsoTpStatus IsoTpEndpoint::process_flow_control(
    const transport::CanFrame& frame,
    const time::MonotonicTime now) noexcept {
  if (tx_state_ != TxState::waiting_flow_control) {
    return IsoTpStatus::ok;
  }

  if (frame.length < 3U) {
    fail_tx(IsoTpStatus::protocol_error);
    return IsoTpStatus::protocol_error;
  }

  const auto pci = byte_value(frame.payload[0]);
  const auto flow_status = static_cast<std::uint8_t>(pci & 0x0FU);

  if (flow_status == kFlowContinueToSend) {
    bool valid_stmin = false;
    const auto stmin = decode_stmin(
        byte_value(frame.payload[2]),
        valid_stmin);
    if (!valid_stmin) {
      fail_tx(IsoTpStatus::protocol_error);
      return IsoTpStatus::protocol_error;
    }

    tx_block_size_ = byte_value(frame.payload[1]);
    tx_block_sent_ = 0U;
    tx_wait_frames_ = 0U;
    tx_stmin_ = stmin;
    tx_next_send_ = now;
    tx_state_ = TxState::sending_consecutive;
    last_tx_status_ = IsoTpStatus::in_progress;
    return IsoTpStatus::in_progress;
  }

  if (flow_status == kFlowWait) {
    ++tx_wait_frames_;
    if (tx_wait_frames_ > config_.max_wait_frames) {
      fail_tx(IsoTpStatus::timeout);
      return IsoTpStatus::timeout;
    }

    tx_deadline_ =
        now + std::chrono::duration_cast<time::MonotonicTime>(
                  config_.flow_control_timeout);
    return IsoTpStatus::in_progress;
  }

  if (flow_status == kFlowOverflow) {
    fail_tx(IsoTpStatus::flow_control_overflow);
    return IsoTpStatus::flow_control_overflow;
  }

  fail_tx(IsoTpStatus::protocol_error);
  return IsoTpStatus::protocol_error;
}

IsoTpStatus IsoTpEndpoint::process_single_frame(
    const transport::CanFrame& frame) noexcept {
  if (rx_active_ || rx_complete_) {
    return IsoTpStatus::busy;
  }

  const auto first = byte_value(frame.payload[0]);
  std::size_t payload_offset = 1U;
  std::size_t length = first & 0x0FU;

  if (length == 0U) {
    if (frame.format != transport::CanFrameFormat::fd ||
        frame.length <= 8U ||
        frame.length < 2U) {
      return IsoTpStatus::protocol_error;
    }

    length = byte_value(frame.payload[1]);
    payload_offset = 2U;

    if (length <= 7U) {
      return IsoTpStatus::protocol_error;
    }
  }

  if (length == 0U ||
      length > kMaxPayloadSize ||
      payload_offset + length > frame.length) {
    return IsoTpStatus::protocol_error;
  }

  std::memcpy(
      rx_payload_.data(),
      frame.payload.data() + payload_offset,
      length);

  rx_length_ = length;
  rx_offset_ = length;
  rx_complete_ = true;
  return IsoTpStatus::ok;
}

IsoTpStatus IsoTpEndpoint::process_first_frame(
    const transport::CanFrame& frame,
    const time::MonotonicTime now) noexcept {
  if (frame.length < 2U) {
    return IsoTpStatus::protocol_error;
  }

  if (rx_active_ || rx_complete_) {
    static_cast<void>(queue_flow_control(kFlowOverflow));
    return IsoTpStatus::busy;
  }

  const auto first = byte_value(frame.payload[0]);
  const auto second = byte_value(frame.payload[1]);

  const std::size_t total_length =
      (static_cast<std::size_t>(first & 0x0FU) << 8U) |
      second;

  if (total_length == 0U) {
    static_cast<void>(queue_flow_control(kFlowOverflow));
    return IsoTpStatus::payload_too_large;
  }

  if (total_length > kMaxPayloadSize) {
    static_cast<void>(queue_flow_control(kFlowOverflow));
    return IsoTpStatus::payload_too_large;
  }

  if (total_length <= single_frame_capacity()) {
    return IsoTpStatus::protocol_error;
  }

  const std::size_t available = frame.length - 2U;
  const std::size_t copy_length =
      std::min(available, total_length);

  if (copy_length == 0U) {
    return IsoTpStatus::protocol_error;
  }

  std::memcpy(
      rx_payload_.data(),
      frame.payload.data() + 2U,
      copy_length);

  rx_length_ = total_length;
  rx_offset_ = copy_length;
  rx_expected_sequence_ = 1U;
  rx_block_received_ = 0U;
  rx_active_ = true;
  rx_deadline_ =
      now + std::chrono::duration_cast<time::MonotonicTime>(
                config_.consecutive_frame_timeout);

  const auto fc_status = queue_flow_control(kFlowContinueToSend);
  if (fc_status != IsoTpStatus::ok) {
    reset_rx_transfer();
    return fc_status;
  }

  return IsoTpStatus::in_progress;
}

IsoTpStatus IsoTpEndpoint::process_consecutive_frame(
    const transport::CanFrame& frame,
    const time::MonotonicTime now) noexcept {
  if (!rx_active_ || frame.length < 2U) {
    return IsoTpStatus::protocol_error;
  }

  const auto sequence =
      static_cast<std::uint8_t>(
          byte_value(frame.payload[0]) & 0x0FU);

  if (sequence != rx_expected_sequence_) {
    reset_rx_transfer();
    return IsoTpStatus::sequence_error;
  }

  const std::size_t remaining = rx_length_ - rx_offset_;
  const std::size_t available = frame.length - 1U;
  const std::size_t copy_length =
      std::min(remaining, available);

  if (copy_length == 0U) {
    reset_rx_transfer();
    return IsoTpStatus::protocol_error;
  }

  std::memcpy(
      rx_payload_.data() + rx_offset_,
      frame.payload.data() + 1U,
      copy_length);

  rx_offset_ += copy_length;
  rx_expected_sequence_ =
      static_cast<std::uint8_t>(
          (rx_expected_sequence_ + 1U) & 0x0FU);
  ++rx_block_received_;

  if (rx_offset_ >= rx_length_) {
    rx_active_ = false;
    rx_complete_ = true;
    return IsoTpStatus::ok;
  }

  rx_deadline_ =
      now + std::chrono::duration_cast<time::MonotonicTime>(
                config_.consecutive_frame_timeout);

  if (config_.rx_block_size != 0U &&
      rx_block_received_ >= config_.rx_block_size) {
    rx_block_received_ = 0U;
    const auto status = queue_flow_control(kFlowContinueToSend);
    if (status != IsoTpStatus::ok) {
      reset_rx_transfer();
      return status;
    }
  }

  return IsoTpStatus::in_progress;
}

IsoTpStatus IsoTpEndpoint::poll_tx(
    const time::MonotonicTime now) noexcept {
  if (tx_state_ == TxState::idle) {
    return IsoTpStatus::idle;
  }

  if (tx_state_ == TxState::waiting_flow_control) {
    return IsoTpStatus::in_progress;
  }

  if (tx_state_ == TxState::single_pending) {
    auto frame = make_base_tx_frame();

    if (tx_length_ <= 7U) {
      frame.payload[0] =
          static_cast<std::byte>(tx_length_);
      std::memcpy(
          frame.payload.data() + 1U,
          tx_payload_.data(),
          tx_length_);
      frame.length = choose_wire_length(
          static_cast<std::uint8_t>(tx_length_ + 1U));
    } else {
      frame.payload[0] = std::byte{0x00};
      frame.payload[1] =
          static_cast<std::byte>(tx_length_);
      std::memcpy(
          frame.payload.data() + 2U,
          tx_payload_.data(),
          tx_length_);
      frame.length = choose_wire_length(
          static_cast<std::uint8_t>(tx_length_ + 2U));
    }

    if (frame.length == 0U) {
      fail_tx(IsoTpStatus::protocol_error);
      return IsoTpStatus::protocol_error;
    }

    const auto can_status = can_.send(frame);
    if (can_status == transport::CanStatus::would_block) {
      return IsoTpStatus::would_block;
    }
    if (can_status != transport::CanStatus::ok) {
      const auto mapped = map_can_status(can_status);
      fail_tx(mapped);
      return mapped;
    }

    tx_state_ = TxState::idle;
    last_tx_status_ = IsoTpStatus::ok;
    return IsoTpStatus::ok;
  }

  if (tx_state_ == TxState::first_pending) {
    auto frame = make_base_tx_frame();
    frame.payload[0] =
        static_cast<std::byte>(
            kFirstFrame |
            ((tx_length_ >> 8U) & 0x0FU));
    frame.payload[1] =
        static_cast<std::byte>(tx_length_ & 0xFFU);

    const std::size_t capacity =
        static_cast<std::size_t>(config_.tx_data_length) - 2U;
    const std::size_t copy_length =
        std::min(capacity, tx_length_);

    std::memcpy(
        frame.payload.data() + 2U,
        tx_payload_.data(),
        copy_length);
    frame.length = config_.tx_data_length;

    const auto can_status = can_.send(frame);
    if (can_status == transport::CanStatus::would_block) {
      return IsoTpStatus::would_block;
    }
    if (can_status != transport::CanStatus::ok) {
      const auto mapped = map_can_status(can_status);
      fail_tx(mapped);
      return mapped;
    }

    tx_offset_ = copy_length;
    tx_state_ = TxState::waiting_flow_control;
    tx_deadline_ =
        now + std::chrono::duration_cast<time::MonotonicTime>(
                  config_.flow_control_timeout);
    return IsoTpStatus::in_progress;
  }

  if (tx_state_ != TxState::sending_consecutive) {
    return IsoTpStatus::protocol_error;
  }

  if (now < tx_next_send_) {
    return IsoTpStatus::in_progress;
  }

  auto frame = make_base_tx_frame();
  frame.payload[0] =
      static_cast<std::byte>(
          kConsecutiveFrame |
          (tx_sequence_ & 0x0FU));

  const std::size_t remaining = tx_length_ - tx_offset_;
  const std::size_t capacity =
      static_cast<std::size_t>(config_.tx_data_length) - 1U;
  const std::size_t copy_length =
      std::min(remaining, capacity);

  std::memcpy(
      frame.payload.data() + 1U,
      tx_payload_.data() + tx_offset_,
      copy_length);

  frame.length = choose_wire_length(
      static_cast<std::uint8_t>(copy_length + 1U));

  if (frame.length == 0U) {
    fail_tx(IsoTpStatus::protocol_error);
    return IsoTpStatus::protocol_error;
  }

  const auto can_status = can_.send(frame);
  if (can_status == transport::CanStatus::would_block) {
    return IsoTpStatus::would_block;
  }
  if (can_status != transport::CanStatus::ok) {
    const auto mapped = map_can_status(can_status);
    fail_tx(mapped);
    return mapped;
  }

  tx_offset_ += copy_length;
  tx_sequence_ =
      static_cast<std::uint8_t>((tx_sequence_ + 1U) & 0x0FU);
  ++tx_block_sent_;

  if (tx_offset_ >= tx_length_) {
    tx_state_ = TxState::idle;
    last_tx_status_ = IsoTpStatus::ok;
    return IsoTpStatus::ok;
  }

  if (tx_block_size_ != 0U &&
      tx_block_sent_ >= tx_block_size_) {
    tx_block_sent_ = 0U;
    tx_state_ = TxState::waiting_flow_control;
    tx_deadline_ =
        now + std::chrono::duration_cast<time::MonotonicTime>(
                  config_.flow_control_timeout);
    return IsoTpStatus::in_progress;
  }

  tx_next_send_ = now + tx_stmin_;
  return IsoTpStatus::in_progress;
}

IsoTpStatus IsoTpEndpoint::send_pending_control() noexcept {
  if (!pending_control_) {
    return IsoTpStatus::ok;
  }

  const auto status = can_.send(control_frame_);
  if (status == transport::CanStatus::would_block) {
    return IsoTpStatus::would_block;
  }

  if (status != transport::CanStatus::ok) {
    return map_can_status(status);
  }

  pending_control_ = false;
  control_frame_ = {};
  return IsoTpStatus::ok;
}

IsoTpStatus IsoTpEndpoint::queue_flow_control(
    const std::uint8_t flow_status) noexcept {
  if (pending_control_) {
    return IsoTpStatus::busy;
  }

  auto frame = make_base_tx_frame();
  frame.payload[0] =
      static_cast<std::byte>(
          kFlowControl | (flow_status & 0x0FU));
  frame.payload[1] =
      static_cast<std::byte>(config_.rx_block_size);
  frame.payload[2] =
      static_cast<std::byte>(config_.rx_stmin);
  frame.length = choose_wire_length(3U);

  if (frame.length == 0U) {
    return IsoTpStatus::protocol_error;
  }

  control_frame_ = frame;
  pending_control_ = true;
  return IsoTpStatus::ok;
}

transport::CanFrame IsoTpEndpoint::make_base_tx_frame() const noexcept {
  transport::CanFrame frame{};
  frame.identifier = address_.tx_id;
  frame.identifier_format = address_.identifier_format;
  frame.format = config_.frame_format;
  frame.type = transport::CanFrameType::data;
  frame.bit_rate_switch =
      config_.frame_format == transport::CanFrameFormat::fd &&
      config_.bit_rate_switch;
  return frame;
}

std::uint8_t IsoTpEndpoint::choose_wire_length(
    const std::uint8_t used_bytes) const noexcept {
  if (used_bytes > config_.tx_data_length) {
    return 0U;
  }

  if (config_.frame_format == transport::CanFrameFormat::classic) {
    return used_bytes <= 8U ? used_bytes : 0U;
  }

  if (used_bytes <= 8U) {
    return used_bytes;
  }

  constexpr std::uint8_t allowed[] = {
      12U, 16U, 20U, 24U, 32U, 48U, 64U};

  for (const auto candidate : allowed) {
    if (candidate >= used_bytes &&
        candidate <= config_.tx_data_length) {
      return candidate;
    }
  }

  return 0U;
}

std::size_t IsoTpEndpoint::single_frame_capacity() const noexcept {
  if (config_.frame_format == transport::CanFrameFormat::classic ||
      config_.tx_data_length <= 8U) {
    return 7U;
  }

  return static_cast<std::size_t>(config_.tx_data_length) - 2U;
}

IsoTpStatus IsoTpEndpoint::map_can_status(
    const transport::CanStatus status) const noexcept {
  switch (status) {
    case transport::CanStatus::ok:
      return IsoTpStatus::ok;
    case transport::CanStatus::would_block:
      return IsoTpStatus::would_block;
    case transport::CanStatus::bus_off:
      return IsoTpStatus::bus_off;
    case transport::CanStatus::not_open:
    case transport::CanStatus::invalid_argument:
    case transport::CanStatus::unsupported:
    case transport::CanStatus::io_error:
      return IsoTpStatus::transport_error;
  }

  return IsoTpStatus::transport_error;
}

void IsoTpEndpoint::fail_tx(const IsoTpStatus status) noexcept {
  tx_state_ = TxState::idle;
  last_tx_status_ = status;
  tx_length_ = 0U;
  tx_offset_ = 0U;
}

void IsoTpEndpoint::reset_rx_transfer() noexcept {
  rx_active_ = false;
  rx_length_ = 0U;
  rx_offset_ = 0U;
  rx_expected_sequence_ = 1U;
  rx_block_received_ = 0U;
  rx_deadline_ = time::MonotonicTime{0};
}

}  // namespace ecu::core::protocol::isotp
