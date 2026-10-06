#include "ecu/core/protocol/uds/uds_client.hpp"

#include "ecu/core/protocol/uds/uds_services.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ecu::core::protocol::uds {
namespace {

std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

time::MonotonicTime to_monotonic(
    const std::chrono::milliseconds value) noexcept {
  return std::chrono::duration_cast<time::MonotonicTime>(value);
}

}  // namespace

UdsClient::UdsClient(
    isotp::IsoTpEndpoint& transport,
    const time::IMonotonicClock& clock,
    const UdsTiming timing) noexcept
    : transport_(transport),
      clock_(clock),
      timing_(timing),
      valid_(
          transport.valid() &&
          is_valid_uds_timing(timing)) {}

bool UdsClient::valid() const noexcept {
  return valid_;
}

bool UdsClient::busy() const noexcept {
  return state_ == State::sending ||
         state_ == State::waiting_p2 ||
         state_ == State::waiting_p2_star;
}

UdsStatus UdsClient::start_request(
    const std::byte* payload,
    const std::size_t length) noexcept {
  if (!valid_ || payload == nullptr || length == 0U) {
    return UdsStatus::invalid_argument;
  }

  if (length > kMaxUdsPayloadSize) {
    return UdsStatus::payload_too_large;
  }

  if (busy() || response_ready_) {
    return UdsStatus::busy;
  }

  request_sid_ = byte_value(payload[0]);
  response_ = {};
  response_ready_ = false;
  deadline_ = time::MonotonicTime{0};

  const auto status = transport_.start_send(payload, length);
  if (status != isotp::IsoTpStatus::in_progress) {
    return map_transport_status(status);
  }

  state_ = State::sending;
  return UdsStatus::in_progress;
}

UdsStatus UdsClient::start_request(
    const UdsRequest& request) noexcept {
  if (request.length == 0U) {
    return UdsStatus::invalid_argument;
  }

  return start_request(
      request.payload.data(),
      request.length);
}

UdsStatus UdsClient::poll() noexcept {
  if (!valid_) {
    return UdsStatus::invalid_argument;
  }

  if (state_ == State::idle && !response_ready_) {
    return UdsStatus::idle;
  }

  const auto transport_status = transport_.poll();
  if (transport_status != isotp::IsoTpStatus::ok &&
      transport_status != isotp::IsoTpStatus::idle &&
      transport_status != isotp::IsoTpStatus::in_progress &&
      transport_status != isotp::IsoTpStatus::would_block) {
    const auto mapped = map_transport_status(transport_status);
    complete_with_status(mapped);
    return mapped;
  }

  const auto now = clock_.now();

  if (state_ == State::sending &&
      !transport_.tx_busy()) {
    const auto tx_status = transport_.last_tx_status();
    if (tx_status != isotp::IsoTpStatus::ok) {
      const auto mapped = map_transport_status(tx_status);
      complete_with_status(mapped);
      return mapped;
    }

    state_ = State::waiting_p2;
    deadline_ = now + to_monotonic(timing_.p2);
  }

  if (transport_.has_received()) {
    const auto received = transport_.take_received();
    const auto status =
        handle_transport_response(received, now);
    if (status != UdsStatus::in_progress) {
      return status;
    }
  }

  if (state_ == State::waiting_p2 &&
      now >= deadline_) {
    complete_with_status(UdsStatus::timeout_p2);
    return UdsStatus::timeout_p2;
  }

  if (state_ == State::waiting_p2_star &&
      now >= deadline_) {
    complete_with_status(UdsStatus::timeout_p2_star);
    return UdsStatus::timeout_p2_star;
  }

  if (busy()) {
    return UdsStatus::in_progress;
  }

  return response_ready_
             ? response_.status
             : UdsStatus::idle;
}

bool UdsClient::has_response() const noexcept {
  return response_ready_;
}

UdsResponse UdsClient::take_response() noexcept {
  if (!response_ready_) {
    return UdsResponse{};
  }

  auto result = response_;
  response_ = {};
  response_ready_ = false;
  state_ = State::idle;
  request_sid_ = 0U;
  deadline_ = time::MonotonicTime{0};
  return result;
}

UdsTiming UdsClient::timing() const noexcept {
  return timing_;
}

void UdsClient::set_timing(const UdsTiming timing) noexcept {
  if (!busy() && is_valid_uds_timing(timing)) {
    timing_ = timing;
    valid_ = transport_.valid();
  }
}

void UdsClient::reset() noexcept {
  transport_.reset();
  state_ = State::idle;
  request_sid_ = 0U;
  deadline_ = time::MonotonicTime{0};
  response_ = {};
  response_ready_ = false;
}

UdsStatus UdsClient::handle_transport_response(
    const isotp::IsoTpReceiveResult& received,
    const time::MonotonicTime now) noexcept {
  if ((state_ != State::waiting_p2 &&
       state_ != State::waiting_p2_star) ||
      received.status != isotp::IsoTpStatus::ok ||
      received.length == 0U) {
    complete_with_status(UdsStatus::protocol_error);
    return UdsStatus::protocol_error;
  }

  const auto first = byte_value(received.payload[0]);

  if (first == kNegativeResponseSid) {
    if (received.length < 3U ||
        byte_value(received.payload[1]) != request_sid_) {
      complete_with_status(UdsStatus::protocol_error);
      return UdsStatus::protocol_error;
    }

    const auto nrc = byte_value(received.payload[2]);
    if (nrc ==
        static_cast<std::uint8_t>(
            UdsNegativeResponseCode::response_pending)) {
      state_ = State::waiting_p2_star;
      deadline_ = now + to_monotonic(timing_.p2_star);
      return UdsStatus::in_progress;
    }

    response_ = {};
    response_.status = UdsStatus::negative_response;
    response_.request_sid = request_sid_;
    response_.response_sid = kNegativeResponseSid;
    response_.negative_response_code = nrc;
    response_.length = received.length;
    std::memcpy(
        response_.payload.data(),
        received.payload.data(),
        received.length);
    response_ready_ = true;
    state_ = State::complete;
    return UdsStatus::negative_response;
  }

  const auto expected =
      positive_response_sid(request_sid_);

  if (first != expected) {
    complete_with_status(UdsStatus::protocol_error);
    return UdsStatus::protocol_error;
  }

  response_ = {};
  response_.status = UdsStatus::ok;
  response_.request_sid = request_sid_;
  response_.response_sid = first;
  response_.length = received.length;
  std::memcpy(
      response_.payload.data(),
      received.payload.data(),
      received.length);
  response_ready_ = true;
  state_ = State::complete;

  if (request_sid_ == kSidDiagnosticSessionControl) {
    UdsTiming new_timing{};
    if (parse_session_control_timing(
            response_,
            new_timing)) {
      timing_ = new_timing;
    }
  }

  return UdsStatus::ok;
}

UdsStatus UdsClient::map_transport_status(
    const isotp::IsoTpStatus status) const noexcept {
  switch (status) {
    case isotp::IsoTpStatus::ok:
      return UdsStatus::ok;
    case isotp::IsoTpStatus::idle:
      return UdsStatus::idle;
    case isotp::IsoTpStatus::in_progress:
    case isotp::IsoTpStatus::would_block:
      return UdsStatus::in_progress;
    case isotp::IsoTpStatus::invalid_argument:
      return UdsStatus::invalid_argument;
    case isotp::IsoTpStatus::payload_too_large:
      return UdsStatus::payload_too_large;
    case isotp::IsoTpStatus::timeout:
    case isotp::IsoTpStatus::sequence_error:
    case isotp::IsoTpStatus::flow_control_overflow:
    case isotp::IsoTpStatus::protocol_error:
    case isotp::IsoTpStatus::transport_error:
    case isotp::IsoTpStatus::bus_off:
    case isotp::IsoTpStatus::busy:
      return UdsStatus::transport_error;
  }

  return UdsStatus::transport_error;
}

void UdsClient::complete_with_status(
    const UdsStatus status) noexcept {
  response_ = {};
  response_.status = status;
  response_.request_sid = request_sid_;
  response_.length = 0U;
  response_ready_ = true;
  state_ = State::complete;
}

}  // namespace ecu::core::protocol::uds
