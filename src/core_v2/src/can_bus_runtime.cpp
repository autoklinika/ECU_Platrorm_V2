#include "ecu/core_v2/transport/can_bus_runtime.hpp"

namespace ecu::core::v2::transport {

bool CanFilter::valid() const noexcept {
  const auto limit = match_extended ? 0x1FFFFFFFU : 0x7FFU;
  return (match_standard || match_extended) &&
         identifier <= limit && mask <= limit;
}

bool CanFilter::matches(const CanFrame& frame) const noexcept {
  if (!valid() || !is_valid_can_frame(frame)) {
    return false;
  }

  const bool format_allowed =
      (frame.identifier_format == CanIdentifierFormat::standard_11_bit &&
       match_standard) ||
      (frame.identifier_format == CanIdentifierFormat::extended_29_bit &&
       match_extended);

  return format_allowed &&
         (frame.identifier & mask) == (identifier & mask);
}

CanBusRuntime::CanBusRuntime(ICanDriver& driver) noexcept
    : driver_(driver) {}

CanBusRuntime::~CanBusRuntime() noexcept {
  if (owns_driver_lease_) {
    driver_.close();
    driver_.release_lease(this);
    owns_driver_lease_ = false;
  }
}

CanSubscriptionResult CanBusRuntime::subscribe(
    const CanFilter& filter,
    ICanFrameSink& sink) noexcept {
  if (!configuration_.accepts_registration() ||
      state_ != CanBusState::configuring) {
    return {
        CanSubscriptionStatus::configuration_frozen,
        {}};
  }

  if (!filter.valid()) {
    return {CanSubscriptionStatus::invalid_argument, {}};
  }

  for (std::size_t i = 0U; i < subscriptions_.size(); ++i) {
    auto& entry = subscriptions_[i];
    if (entry.used) {
      continue;
    }

    auto generation = next_generation_++;
    if (generation == 0U) {
      generation = next_generation_++;
    }

    entry.used = true;
    entry.generation = generation;
    entry.filter = filter;
    entry.sink = &sink;

    return {
        CanSubscriptionStatus::subscribed,
        CanSubscriptionHandle{
            static_cast<std::uint16_t>(i),
            generation}};
  }

  return {
      CanSubscriptionStatus::capacity_exhausted,
      {}};
}

bool CanBusRuntime::freeze_configuration() noexcept {
  if (state_ != CanBusState::configuring ||
      !configuration_.freeze()) {
    return false;
  }

  state_ = CanBusState::ready;
  return true;
}

CanStatus CanBusRuntime::start(
    const CanChannelConfig& config) noexcept {
  if (dispatching_) {
    return CanStatus::busy;
  }
  if (state_ == CanBusState::faulted) {
    return CanStatus::faulted;
  }
  if (state_ == CanBusState::running) {
    return CanStatus::already_open;
  }
  if (state_ != CanBusState::ready &&
      state_ != CanBusState::stopped) {
    return CanStatus::invalid_state;
  }

  if (!is_valid_can_channel_config(config)) {
    return CanStatus::invalid_argument;
  }
  const auto capabilities = driver_.capabilities();
  if (!capabilities_support(capabilities, config)) {
    return CanStatus::unsupported;
  }
  if (!driver_.try_acquire_lease(this)) {
    return CanStatus::busy;
  }
  owns_driver_lease_ = true;

  if (driver_.is_open()) {
    driver_.release_lease(this);
    owns_driver_lease_ = false;
    return CanStatus::already_open;
  }

  const auto status = driver_.open(config);
  if (status != CanStatus::ok) {
    driver_.release_lease(this);
    owns_driver_lease_ = false;
    latch_fault(status);
    return status;
  }
  active_config_ = config;
  active_capabilities_ = capabilities;

  state_ = CanBusState::running;
  return CanStatus::ok;
}

void CanBusRuntime::stop() noexcept {
  if (dispatching_) { stop_pending_ = true; return; }
  if (state_ == CanBusState::faulted) { return; }
  if (owns_driver_lease_) {
    driver_.close();
    driver_.release_lease(this);
    owns_driver_lease_ = false;
  }

  if (state_ != CanBusState::faulted) {
    state_ = configuration_.state() ==
                     runtime::ConfigurationState::frozen
                 ? CanBusState::stopped
                 : CanBusState::configuring;
  }
}

CanStatus CanBusRuntime::send(
    const CanFrame& frame) noexcept {
  if (dispatching_) {
    return CanStatus::busy;
  }
  if (state_ == CanBusState::faulted) {
    return CanStatus::faulted;
  }
  if (state_ != CanBusState::running) {
    return CanStatus::not_open;
  }

  const auto validation = validate_frame(frame);
  if (validation != CanStatus::ok) { return validation; }
  if (active_config_.mode == CanMode::listen_only) {
    return CanStatus::unsupported;
  }
  const auto status = driver_.try_send(frame);
  latch_fault(status);
  return status;
}

CanPollResult CanBusRuntime::poll(
    const std::size_t max_frames) noexcept {
  CanPollResult result{};

  if (dispatching_) {
    result.status = CanStatus::busy;
    return result;
  }
  if (state_ == CanBusState::faulted) {
    result.status = CanStatus::faulted;
    return result;
  }
  if (state_ != CanBusState::running) {
    result.status = CanStatus::not_open;
    return result;
  }
  if (max_frames == 0U) {
    result.status = CanStatus::invalid_argument;
    return result;
  }

  for (std::size_t i = 0U; i < max_frames; ++i) {
    const auto received = driver_.try_receive();

    if (received.status == CanStatus::would_block) {
      result.status = CanStatus::ok;
      return result;
    }

    if (received.status != CanStatus::ok) {
      result.status = received.status;
      latch_fault(received.status);
      return result;
    }

    ++result.frames_received;
    result.status = validate_frame(received.value.frame);
    if (result.status != CanStatus::ok) { return result; }
    result.deliveries += dispatch(received.value);
    if (stop_pending_) {
      stop_pending_ = false;
      stop();
      return result;
    }
  }

  result.status = CanStatus::ok;
  return result;
}

CanStatus CanBusRuntime::recover() noexcept {
  if (dispatching_) {
    return CanStatus::busy;
  }
  if (state_ != CanBusState::faulted) {
    return CanStatus::invalid_state;
  }
  if (owns_driver_lease_) {
    driver_.close();
    driver_.release_lease(this);
    owns_driver_lease_ = false;
  }
  state_ = CanBusState::stopped;
  return CanStatus::ok;
}

void CanBusRuntime::latch_fault(const CanStatus status) noexcept {
  if (status == CanStatus::bus_off || status == CanStatus::io_error ||
      status == CanStatus::not_open) {
    state_ = CanBusState::faulted;
  }
}

CanStatus CanBusRuntime::validate_frame(const CanFrame& frame) const noexcept {
  if (!is_valid_can_frame(frame)) {
    return CanStatus::invalid_frame;
  }
  if ((frame.format == CanFrameFormat::fd && !active_config_.fd_enabled) ||
      !capabilities_support_frame(active_capabilities_, frame)) {
    return CanStatus::unsupported;
  }
  return CanStatus::ok;
}

CanBusState CanBusRuntime::state() const noexcept {
  return state_;
}

std::size_t CanBusRuntime::subscription_count() const noexcept {
  std::size_t count = 0U;
  for (const auto& entry : subscriptions_) {
    if (entry.used) {
      ++count;
    }
  }
  return count;
}

std::size_t CanBusRuntime::dispatch(
    const ReceivedCanFrame& frame) noexcept {
  dispatching_ = true;
  std::size_t deliveries = 0U;

  for (auto& entry : subscriptions_) {
    if (!entry.used || entry.sink == nullptr ||
        !entry.filter.matches(frame.frame)) {
      continue;
    }

    entry.sink->on_can_frame(frame);
    ++deliveries;
  }

  dispatching_ = false;
  return deliveries;
}

}  // namespace ecu::core::v2::transport
