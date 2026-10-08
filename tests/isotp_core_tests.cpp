#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using namespace ecu::core;
using namespace ecu::core::protocol::isotp;
using namespace ecu::core::transport;

class FakeClock final : public time::IMonotonicClock {
 public:
  time::MonotonicTime now() const noexcept override {
    return now_;
  }

  template <typename Rep, typename Period>
  void advance(const std::chrono::duration<Rep, Period> delta) noexcept {
    now_ += std::chrono::duration_cast<time::MonotonicTime>(delta);
  }

 private:
  time::MonotonicTime now_{0};
};

struct SentFrame {
  CanFrame frame{};
  time::MonotonicTime timestamp{};
};

class FakeCan final : public ICanInterface {
 public:
  explicit FakeCan(FakeClock& clock) : clock_(clock) {}

  void connect(FakeCan& peer) noexcept {
    peer_ = &peer;
  }

  CanCapabilities capabilities() const noexcept override {
    return CanCapabilities{true, true, true, true, 64U};
  }

  CanStatus open(const CanChannelConfig&) noexcept override {
    open_ = true;
    return CanStatus::ok;
  }

  void close() noexcept override {
    open_ = false;
  }

  bool is_open() const noexcept override {
    return open_;
  }

  CanStatus send(const CanFrame& frame) noexcept override {
    if (!open_) {
      return CanStatus::not_open;
    }
    if (!is_valid_can_frame(frame)) {
      return CanStatus::invalid_argument;
    }

    sent_.push_back(SentFrame{frame, clock_.now()});

    if (peer_ != nullptr) {
      peer_->rx_.push_back(
          ReceivedCanFrame{frame, clock_.now()});
    }
    return CanStatus::ok;
  }

  CanReceiveResult try_receive() noexcept override {
    if (!open_) {
      return CanReceiveResult{CanStatus::not_open, {}};
    }

    if (rx_.empty()) {
      return CanReceiveResult{CanStatus::would_block, {}};
    }

    const auto value = rx_.front();
    rx_.pop_front();
    return CanReceiveResult{CanStatus::ok, value};
  }

  void inject(const CanFrame& frame) {
    rx_.push_back(ReceivedCanFrame{frame, clock_.now()});
  }

  const std::vector<SentFrame>& sent() const noexcept {
    return sent_;
  }

 private:
  FakeClock& clock_;
  FakeCan* peer_{nullptr};
  bool open_{true};
  std::deque<ReceivedCanFrame> rx_{};
  std::vector<SentFrame> sent_{};
};

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

IsoTpAddress address_a() {
  return IsoTpAddress{
      0x700U,
      0x708U,
      CanIdentifierFormat::standard_11_bit};
}

IsoTpAddress address_b() {
  return IsoTpAddress{
      0x708U,
      0x700U,
      CanIdentifierFormat::standard_11_bit};
}

IsoTpConfig classic_config() {
  IsoTpConfig config{};
  config.frame_format = CanFrameFormat::classic;
  config.tx_data_length = 8U;
  config.flow_control_timeout = std::chrono::milliseconds{50};
  config.consecutive_frame_timeout = std::chrono::milliseconds{50};
  return config;
}

IsoTpConfig fd_config() {
  IsoTpConfig config{};
  config.frame_format = CanFrameFormat::fd;
  config.tx_data_length = 64U;
  config.bit_rate_switch = true;
  config.flow_control_timeout = std::chrono::milliseconds{50};
  config.consecutive_frame_timeout = std::chrono::milliseconds{50};
  return config;
}

bool pump(
    IsoTpEndpoint& a,
    IsoTpEndpoint& b,
    FakeClock& clock,
    const std::size_t iterations,
    const std::chrono::microseconds step =
        std::chrono::microseconds{100}) {
  for (std::size_t i = 0U; i < iterations; ++i) {
    const auto a_status = a.poll();
    const auto b_status = b.poll();

    const auto fatal = [](const IsoTpStatus status) {
      return status != IsoTpStatus::ok &&
             status != IsoTpStatus::in_progress &&
             status != IsoTpStatus::idle &&
             status != IsoTpStatus::would_block;
    };

    if (fatal(a_status) || fatal(b_status)) {
      return false;
    }

    if (!a.tx_busy() && b.has_received()) {
      return true;
    }

    clock.advance(step);
  }

  return false;
}

bool payload_equals(
    const IsoTpReceiveResult& result,
    const std::vector<std::byte>& expected) {
  return result.status == IsoTpStatus::ok &&
         result.length == expected.size() &&
         std::memcmp(
             result.payload.data(),
             expected.data(),
             expected.size()) == 0;
}

CanFrame classic_frame(
    const std::uint32_t identifier,
    const std::initializer_list<std::uint8_t> bytes) {
  CanFrame frame{};
  frame.identifier = identifier;
  frame.identifier_format = CanIdentifierFormat::standard_11_bit;
  frame.format = CanFrameFormat::classic;
  frame.length = static_cast<std::uint8_t>(bytes.size());

  std::size_t index = 0U;
  for (const auto value : bytes) {
    frame.payload[index++] = static_cast<std::byte>(value);
  }
  return frame;
}

}  // namespace

int main() {
  int failures = 0;

  {
    bool valid = false;
    failures += require(
        decode_stmin(0x05U, valid) ==
                std::chrono::milliseconds{5} &&
            valid,
        "STmin 0x05 = 5 ms");

    failures += require(
        decode_stmin(0xF3U, valid) ==
                std::chrono::microseconds{300} &&
            valid,
        "STmin 0xF3 = 300 us");

    static_cast<void>(decode_stmin(0x80U, valid));
    failures += require(!valid, "reserved STmin rejected");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    FakeCan can_b{clock};
    can_a.connect(can_b);
    can_b.connect(can_a);

    auto config = classic_config();
    IsoTpEndpoint a{can_a, clock, address_a(), config};
    IsoTpEndpoint b{can_b, clock, address_b(), config};

    const std::vector<std::byte> payload{
        std::byte{0x22}, std::byte{0xF1}, std::byte{0x90}};

    failures += require(a.valid() && b.valid(), "classic endpoints valid");
    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "classic SF start");
    failures += require(
        pump(a, b, clock, 20U),
        "classic SF end-to-end");

    const auto received = b.take_received();
    failures += require(
        payload_equals(received, payload),
        "classic SF payload preserved");
    failures += require(
        can_a.sent().size() == 1U,
        "classic SF uses one CAN frame");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    FakeCan can_b{clock};
    can_a.connect(can_b);
    can_b.connect(can_a);

    auto tx_config = classic_config();
    auto rx_config = classic_config();
    rx_config.rx_block_size = 2U;

    IsoTpEndpoint a{can_a, clock, address_a(), tx_config};
    IsoTpEndpoint b{can_b, clock, address_b(), rx_config};

    std::vector<std::byte> payload(30U);
    for (std::size_t i = 0U; i < payload.size(); ++i) {
      payload[i] = static_cast<std::byte>(i);
    }

    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "classic MF start");
    failures += require(
        pump(a, b, clock, 300U),
        "classic MF end-to-end with block FC");

    const auto received = b.take_received();
    failures += require(
        payload_equals(received, payload),
        "classic MF payload preserved");

    std::size_t flow_controls = 0U;
    for (const auto& sent : can_b.sent()) {
      if ((std::to_integer<std::uint8_t>(sent.frame.payload[0]) &
           0xF0U) == 0x30U) {
        ++flow_controls;
      }
    }
    failures += require(
        flow_controls >= 2U,
        "block size forces repeated FC");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    FakeCan can_b{clock};
    can_a.connect(can_b);
    can_b.connect(can_a);

    auto config = fd_config();
    IsoTpEndpoint a{can_a, clock, address_a(), config};
    IsoTpEndpoint b{can_b, clock, address_b(), config};

    std::vector<std::byte> payload(20U);
    for (std::size_t i = 0U; i < payload.size(); ++i) {
      payload[i] = static_cast<std::byte>(0xA0U + i);
    }

    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "FD SF start");
    failures += require(
        pump(a, b, clock, 30U),
        "FD SF end-to-end");

    const auto received = b.take_received();
    failures += require(
        payload_equals(received, payload),
        "FD SF payload preserved");
    failures += require(
        can_a.sent().size() == 1U,
        "FD 20-byte payload uses one SF");
    failures += require(
        can_a.sent().front().frame.length == 24U,
        "FD SF uses legal 24-byte wire length");
    failures += require(
        std::to_integer<std::uint8_t>(
            can_a.sent().front().frame.payload[0]) == 0x00U &&
        std::to_integer<std::uint8_t>(
            can_a.sent().front().frame.payload[1]) == 20U,
        "FD SF escape length encoded");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    FakeCan can_b{clock};
    can_a.connect(can_b);
    can_b.connect(can_a);

    auto tx_config = fd_config();
    auto rx_config = fd_config();
    rx_config.rx_block_size = 1U;
    rx_config.rx_stmin = 0xF3U;

    IsoTpEndpoint a{can_a, clock, address_a(), tx_config};
    IsoTpEndpoint b{can_b, clock, address_b(), rx_config};

    std::vector<std::byte> payload(180U);
    for (std::size_t i = 0U; i < payload.size(); ++i) {
      payload[i] = static_cast<std::byte>(i & 0xFFU);
    }

    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "FD MF start");
    failures += require(
        pump(
            a,
            b,
            clock,
            2000U,
            std::chrono::microseconds{100}),
        "FD MF with block size and STmin");

    const auto received = b.take_received();
    failures += require(
        payload_equals(received, payload),
        "FD MF payload preserved");

    std::vector<time::MonotonicTime> cf_times;
    for (const auto& sent : can_a.sent()) {
      if ((std::to_integer<std::uint8_t>(sent.frame.payload[0]) &
           0xF0U) == 0x20U) {
        cf_times.push_back(sent.timestamp);
      }
    }

    failures += require(cf_times.size() >= 2U, "multiple FD CF frames sent");
    for (std::size_t i = 1U; i < cf_times.size(); ++i) {
      failures += require(
          cf_times[i] - cf_times[i - 1U] >=
              std::chrono::microseconds{300},
          "sender respects received STmin");
    }
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    auto config = classic_config();
    config.flow_control_timeout = std::chrono::milliseconds{10};

    IsoTpEndpoint a{can_a, clock, address_a(), config};

    std::vector<std::byte> payload(20U, std::byte{0x55});
    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "timeout transfer start");

    failures += require(
        a.poll() == IsoTpStatus::in_progress,
        "first frame sent and waits for FC");

    clock.advance(std::chrono::milliseconds{11});
    failures += require(
        a.poll() == IsoTpStatus::timeout,
        "flow-control timeout detected");
    failures += require(
        a.last_tx_status() == IsoTpStatus::timeout,
        "timeout stored as TX result");
  }

  {
    FakeClock clock;
    FakeCan can_b{clock};
    auto config = classic_config();
    IsoTpEndpoint b{can_b, clock, address_b(), config};

    const auto ff = classic_frame(
        0x700U,
        {0x10U, 0x14U, 1U, 2U, 3U, 4U, 5U, 6U});
    can_b.inject(ff);
    failures += require(
        b.poll() == IsoTpStatus::in_progress,
        "RX accepts first frame");

    const auto wrong_cf = classic_frame(
        0x700U,
        {0x22U, 7U, 8U, 9U, 10U, 11U, 12U, 13U});
    can_b.inject(wrong_cf);
    failures += require(
        b.poll() == IsoTpStatus::sequence_error,
        "sequence mismatch rejected");
  }


  {
    FakeClock clock;
    FakeCan can_a{clock};
    FakeCan can_b{clock};
    can_a.connect(can_b);
    can_b.connect(can_a);

    auto config = classic_config();
    const IsoTpAddress ext_a{
        0x18DAF110U,
        0x18DA10F1U,
        CanIdentifierFormat::extended_29_bit};
    const IsoTpAddress ext_b{
        0x18DA10F1U,
        0x18DAF110U,
        CanIdentifierFormat::extended_29_bit};

    IsoTpEndpoint a{can_a, clock, ext_a, config};
    IsoTpEndpoint b{can_b, clock, ext_b, config};

    const std::vector<std::byte> payload{
        std::byte{0x10}, std::byte{0x03}};

    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "29-bit SF start");
    failures += require(
        pump(a, b, clock, 20U),
        "29-bit SF end-to-end");
    failures += require(
        payload_equals(b.take_received(), payload),
        "29-bit ISO-TP addressing preserved");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    auto config = classic_config();
    IsoTpEndpoint a{can_a, clock, address_a(), config};

    std::vector<std::byte> payload(20U, std::byte{0x11});
    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "FC overflow transfer start");
    failures += require(
        a.poll() == IsoTpStatus::in_progress,
        "FC overflow waits after FF");

    can_a.inject(classic_frame(
        0x708U,
        {0x32U, 0x00U, 0x00U}));

    failures += require(
        a.poll() == IsoTpStatus::flow_control_overflow,
        "FC overflow terminates sender");
    failures += require(
        a.last_tx_status() == IsoTpStatus::flow_control_overflow,
        "FC overflow stored as TX result");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    auto config = classic_config();
    config.max_wait_frames = 1U;
    IsoTpEndpoint a{can_a, clock, address_a(), config};

    std::vector<std::byte> payload(20U, std::byte{0x22});
    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "FC wait transfer start");
    failures += require(
        a.poll() == IsoTpStatus::in_progress,
        "FC wait first frame sent");

    can_a.inject(classic_frame(
        0x708U,
        {0x31U, 0x00U, 0x00U}));
    failures += require(
        a.poll() == IsoTpStatus::in_progress,
        "first FC Wait accepted");

    can_a.inject(classic_frame(
        0x708U,
        {0x31U, 0x00U, 0x00U}));
    failures += require(
        a.poll() == IsoTpStatus::timeout,
        "excess FC Wait rejected");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    FakeCan can_b{clock};
    can_a.connect(can_b);
    can_b.connect(can_a);

    auto tx_config = classic_config();
    auto rx_config = classic_config();
    rx_config.rx_block_size = 8U;

    IsoTpEndpoint a{can_a, clock, address_a(), tx_config};
    IsoTpEndpoint b{can_b, clock, address_b(), rx_config};

    std::vector<std::byte> payload(kMaxPayloadSize);
    for (std::size_t i = 0U; i < payload.size(); ++i) {
      payload[i] = static_cast<std::byte>(
          static_cast<std::uint8_t>((i * 17U) & 0xFFU));
    }

    failures += require(
        a.start_send(payload.data(), payload.size()) ==
            IsoTpStatus::in_progress,
        "4095-byte transfer start");
    failures += require(
        pump(
            a,
            b,
            clock,
            10000U,
            std::chrono::microseconds{100}),
        "4095-byte transfer completes");
    failures += require(
        payload_equals(b.take_received(), payload),
        "4095-byte payload preserved across sequence wrap");
    failures += require(
        a.last_tx_status() == IsoTpStatus::ok,
        "4095-byte sender completes cleanly");
  }

  {
    FakeClock clock;
    FakeCan can_b{clock};
    auto config = classic_config();
    IsoTpEndpoint b{can_b, clock, address_b(), config};

    CanFrame extended_ff{};
    extended_ff.identifier = 0x700U;
    extended_ff.identifier_format =
        CanIdentifierFormat::standard_11_bit;
    extended_ff.format = CanFrameFormat::classic;
    extended_ff.length = 8U;
    extended_ff.payload[0] = std::byte{0x10};
    extended_ff.payload[1] = std::byte{0x00};
    extended_ff.payload[2] = std::byte{0x00};
    extended_ff.payload[3] = std::byte{0x00};
    extended_ff.payload[4] = std::byte{0x10};
    extended_ff.payload[5] = std::byte{0x00};

    can_b.inject(extended_ff);

    failures += require(
        b.poll() == IsoTpStatus::payload_too_large,
        "extended FF length is rejected by Stage H limit");
    failures += require(
        can_b.sent().size() == 1U,
        "overflow FC emitted for unsupported extended FF");
    failures += require(
        (std::to_integer<std::uint8_t>(
             can_b.sent().front().frame.payload[0]) &
         0x0FU) == 0x02U,
        "overflow FC uses FS=Overflow");
  }

  {
    FakeClock clock;
    FakeCan can_a{clock};
    auto config = classic_config();
    IsoTpEndpoint a{can_a, clock, address_a(), config};

    std::vector<std::byte> too_large(kMaxPayloadSize + 1U);
    failures += require(
        a.start_send(too_large.data(), too_large.size()) ==
            IsoTpStatus::payload_too_large,
        "payload above 4095 rejected");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "ISOTP_CORE_TESTS=PASS\n";
  return 0;
}
