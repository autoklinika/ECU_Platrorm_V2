#include "ecu/core/time/monotonic_clock.hpp"
#include "ecu/core/transport/can_types.hpp"
#include "ecu/core/transport/i_can_interface.hpp"

#include <chrono>
#include <iostream>

namespace {

using ecu::core::time::IMonotonicClock;
using ecu::core::time::MonotonicTime;
using namespace ecu::core::transport;

class FakeClock final : public IMonotonicClock {
 public:
  explicit FakeClock(const MonotonicTime value) : value_(value) {}

  MonotonicTime now() const noexcept override {
    return value_;
  }

 private:
  MonotonicTime value_;
};

class FakeCanInterface final : public ICanInterface {
 public:
  CanCapabilities capabilities() const noexcept override {
    return CanCapabilities{true, true, true, true, 64U};
  }

  CanStatus open(const CanChannelConfig& config) noexcept override {
    if (!capabilities_support(capabilities(), config)) {
      return CanStatus::unsupported;
    }
    config_ = config;
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
    if (config_.mode == CanMode::listen_only) {
      return CanStatus::unsupported;
    }
    if (!is_valid_can_frame(frame)) {
      return CanStatus::invalid_argument;
    }
    if (!capabilities_support_frame(capabilities(), frame)) {
      return CanStatus::unsupported;
    }
    if (frame.format == CanFrameFormat::fd && !config_.fd_enabled) {
      return CanStatus::unsupported;
    }
    return CanStatus::ok;
  }

  CanReceiveResult try_receive() noexcept override {
    return CanReceiveResult{CanStatus::would_block, {}};
  }

 private:
  bool open_{false};
  CanChannelConfig config_{};
};

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int failures = 0;

  CanFrame classic{};
  classic.identifier = 0x7FFU;
  classic.length = 8U;
  failures += require(is_valid_can_frame(classic), "valid classic frame");

  classic.identifier = 0x800U;
  failures += require(!is_valid_can_frame(classic), "reject 12-bit standard id");

  CanFrame extended{};
  extended.identifier_format = CanIdentifierFormat::extended_29_bit;
  extended.identifier = 0x1FFFFFFFU;
  failures += require(is_valid_can_frame(extended), "valid extended id");

  extended.identifier = 0x20000000U;
  failures += require(!is_valid_can_frame(extended), "reject >29-bit id");

  CanFrame fd{};
  fd.format = CanFrameFormat::fd;
  fd.identifier = 0x123U;
  fd.length = 64U;
  fd.bit_rate_switch = true;
  failures += require(is_valid_can_frame(fd), "valid CAN-FD 64-byte frame");

  fd.length = 9U;
  failures += require(!is_valid_can_frame(fd), "reject non-wire CAN-FD length");

  fd.length = 12U;
  failures += require(is_valid_can_frame(fd), "accept CAN-FD 12-byte wire length");

  fd.type = CanFrameType::remote;
  failures += require(!is_valid_can_frame(fd), "reject remote CAN-FD frame");

  CanFrame classic_brs{};
  classic_brs.bit_rate_switch = true;
  failures += require(!is_valid_can_frame(classic_brs), "reject BRS on classic CAN");

  CanChannelConfig classic_config{};
  classic_config.nominal_bitrate = 500000U;
  failures += require(
      is_valid_can_channel_config(classic_config),
      "valid classic channel config");

  CanChannelConfig fd_config{};
  fd_config.nominal_bitrate = 500000U;
  fd_config.fd_enabled = true;
  fd_config.data_bitrate = 2000000U;
  failures += require(
      is_valid_can_channel_config(fd_config),
      "valid CAN-FD channel config");

  const CanCapabilities classic_only{true, false, false, true, 8U};
  failures += require(
      capabilities_support(classic_only, classic_config),
      "classic interface accepts classic config");
  failures += require(
      !capabilities_support(classic_only, fd_config),
      "classic-only interface rejects FD config");

  const CanCapabilities fd_no_listen{true, true, true, false, 64U};
  CanChannelConfig listen_config = fd_config;
  listen_config.mode = CanMode::listen_only;
  failures += require(
      !capabilities_support(fd_no_listen, listen_config),
      "interface without listen-only capability rejects listen mode");

  const CanCapabilities no_brs{true, true, false, true, 64U};
  CanFrame brs_frame{};
  brs_frame.format = CanFrameFormat::fd;
  brs_frame.length = 64U;
  brs_frame.bit_rate_switch = true;
  failures += require(
      !capabilities_support_frame(no_brs, brs_frame),
      "interface without BRS capability rejects BRS frame");

  const CanCapabilities fd_32{true, true, true, true, 32U};
  CanFrame fd_32_frame{};
  fd_32_frame.format = CanFrameFormat::fd;
  fd_32_frame.length = 32U;
  failures += require(
      capabilities_support_frame(fd_32, fd_32_frame),
      "capability max payload accepts matching FD frame");
  fd_32_frame.length = 48U;
  failures += require(
      !capabilities_support_frame(fd_32, fd_32_frame),
      "capability max payload rejects oversized FD frame");

  const CanCapabilities full{true, true, true, true, 64U};
  failures += require(
      capabilities_support(full, fd_config),
      "FD-capable interface accepts FD config");

  FakeCanInterface fake;
  failures += require(
      fake.send(classic) == CanStatus::not_open,
      "send before open rejected");
  failures += require(
      fake.open(fd_config) == CanStatus::ok,
      "fake interface opens FD config");
  failures += require(fake.is_open(), "fake interface reports open");

  CanFrame good_send{};
  good_send.identifier = 0x123U;
  failures += require(
      fake.send(good_send) == CanStatus::ok,
      "valid frame accepted by fake");

  CanFrame fd_send{};
  fd_send.identifier = 0x123U;
  fd_send.format = CanFrameFormat::fd;
  fd_send.length = 64U;
  fd_send.bit_rate_switch = true;
  failures += require(
      fake.send(fd_send) == CanStatus::ok,
      "valid FD/BRS frame accepted by capable fake");

  const auto receive = fake.try_receive();
  failures += require(
      receive.status == CanStatus::would_block,
      "nonblocking receive has explicit would_block state");

  fake.close();
  CanChannelConfig listen_only{};
  listen_only.nominal_bitrate = 500000U;
  listen_only.mode = CanMode::listen_only;
  failures += require(
      fake.open(listen_only) == CanStatus::ok,
      "fake opens listen-only mode");
  failures += require(
      fake.send(good_send) == CanStatus::unsupported,
      "listen-only mode rejects transmit");

  FakeClock clock{std::chrono::milliseconds{1234}};
  failures += require(
      clock.now() == std::chrono::milliseconds{1234},
      "monotonic clock contract is injectable");

  if (failures != 0) {
    return 1;
  }

  std::cout << "CAN_CONTRACT_TESTS=PASS\n";
  return 0;
}
