#include "ecu/core/simulation/simulated_can.hpp"

namespace ecu::core::simulation {

SimulatedCanInterface::SimulatedCanInterface(
    const transport::CanCapabilities capabilities) noexcept
    : capabilities_(capabilities) {}

transport::CanCapabilities SimulatedCanInterface::capabilities()
    const noexcept {
  return capabilities_;
}

transport::CanStatus SimulatedCanInterface::open(
    const transport::CanChannelConfig& config) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!transport::capabilities_support(capabilities_, config)) {
    return transport::CanStatus::unsupported;
  }

  config_ = config;
  open_ = true;
  return transport::CanStatus::ok;
}

void SimulatedCanInterface::close() noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  open_ = false;
}

bool SimulatedCanInterface::is_open() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  return open_;
}

transport::CanStatus SimulatedCanInterface::send(
    const transport::CanFrame& frame) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!open_) {
    return transport::CanStatus::not_open;
  }

  if (config_.mode == transport::CanMode::listen_only) {
    return transport::CanStatus::unsupported;
  }

  if (!transport::is_valid_can_frame(frame)) {
    return transport::CanStatus::invalid_argument;
  }

  if (!transport::capabilities_support_frame(
          capabilities_,
          frame)) {
    return transport::CanStatus::unsupported;
  }

  if (frame.format == transport::CanFrameFormat::fd &&
      !config_.fd_enabled) {
    return transport::CanStatus::unsupported;
  }

  return push(tx_, frame)
             ? transport::CanStatus::ok
             : transport::CanStatus::would_block;
}

transport::CanReceiveResult
SimulatedCanInterface::try_receive() noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!open_) {
    return transport::CanReceiveResult{
        transport::CanStatus::not_open,
        {}};
  }

  transport::ReceivedCanFrame frame{};
  if (!pop(rx_, frame)) {
    return transport::CanReceiveResult{
        transport::CanStatus::would_block,
        {}};
  }

  return transport::CanReceiveResult{
      transport::CanStatus::ok,
      frame};
}

bool SimulatedCanInterface::inject_rx(
    const transport::ReceivedCanFrame& frame) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!transport::is_valid_can_frame(frame.frame)) {
    return false;
  }

  return push(rx_, frame);
}

bool SimulatedCanInterface::take_tx(
    transport::CanFrame& frame) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  return pop(tx_, frame);
}

std::size_t SimulatedCanInterface::pending_rx() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  return rx_.count;
}

std::size_t SimulatedCanInterface::pending_tx() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  return tx_.count;
}

}  // namespace ecu::core::simulation
