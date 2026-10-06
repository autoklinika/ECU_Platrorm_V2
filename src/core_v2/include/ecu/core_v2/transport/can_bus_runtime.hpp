#pragma once

#include "ecu/core_v2/runtime/configuration_gate.hpp"
#include "ecu/core_v2/transport/i_can_driver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::core::v2::transport {

// Identifier/mask bits must fit 29 bits when extended is enabled, otherwise
// 11 bits. At least one identifier format must be enabled.
struct CanFilter {
  std::uint32_t identifier{0U};
  std::uint32_t mask{0U};
  bool match_standard{true};
  bool match_extended{true};

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool matches(const CanFrame& frame) const noexcept;
};

class ICanFrameSink {
 public:
  virtual ~ICanFrameSink() = default;

  // frame is borrowed and valid only for the duration of this callback.
  // Consumers that need deferred processing must copy the required data into
  // their own bounded storage. Response TX is deferred until dispatch returns.
  virtual void on_can_frame(const ReceivedCanFrame& frame) noexcept = 0;
};

struct CanSubscriptionHandle {
  std::uint16_t slot{0U};
  std::uint16_t generation{0U};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return generation != 0U;
  }
};

enum class CanSubscriptionStatus : std::uint8_t {
  subscribed,
  invalid_argument,
  configuration_frozen,
  capacity_exhausted,
};

struct CanSubscriptionResult {
  CanSubscriptionStatus status{CanSubscriptionStatus::invalid_argument};
  CanSubscriptionHandle handle{};
};

enum class CanBusState : std::uint8_t {
  configuring,
  ready,
  running,
  stopped,
  faulted,
};

struct CanPollResult {
  CanStatus status{CanStatus::ok};
  std::size_t frames_received{0U};
  std::size_t deliveries{0U};
};

class CanBusRuntime {
 public:
  static constexpr std::size_t kMaxSubscriptions = 32U;

  // Driver and all registered sinks MUST outlive this runtime object, including
  // stopped intervals and restarts. No unregister or hot-removal is supported.
  // One executor serializes all calls; callbacks must not destroy the runtime.
  // Driver lease ownership is exclusive and remains held while faulted even if
  // the underlying adapter reports the physical channel as closed.
  explicit CanBusRuntime(ICanDriver& driver) noexcept;
  ~CanBusRuntime() noexcept;

  CanBusRuntime(const CanBusRuntime&) = delete;
  CanBusRuntime& operator=(const CanBusRuntime&) = delete;
  CanBusRuntime(CanBusRuntime&&) = delete;
  CanBusRuntime& operator=(CanBusRuntime&&) = delete;

  [[nodiscard]] CanSubscriptionResult subscribe(
      const CanFilter& filter,
      ICanFrameSink& sink) noexcept;

  [[nodiscard]] bool freeze_configuration() noexcept;

  // bus_off/io_error/not_open from open/TX/RX latch faulted. No ordinary
  // driver calls occur while faulted except explicit recover(); stop cannot
  // clear a fault. Destruction closes only a driver successfully opened by this runtime.
  // Failed open never transfers ownership.
  [[nodiscard]] CanStatus start(
      const CanChannelConfig& config) noexcept;
  // During dispatch start/send/poll return busy; stop is deferred until every
  // matching sink receives the current frame, before another RX. A protocol
  // needing response TX records bounded pending work in its callback and sends
  // after the owning executor regains control from poll().
  void stop() noexcept;
  // Only explicit recovery may leave faulted; closes an owned driver and enters stopped.
  [[nodiscard]] CanStatus recover() noexcept;

  [[nodiscard]] CanStatus send(const CanFrame& frame) noexcept;

  [[nodiscard]] CanPollResult poll(
      std::size_t max_frames) noexcept;

  [[nodiscard]] CanBusState state() const noexcept;
  [[nodiscard]] std::size_t subscription_count() const noexcept;

 private:
  struct Subscription {
    bool used{false};
    std::uint16_t generation{0U};
    CanFilter filter{};
    ICanFrameSink* sink{nullptr};
  };

  [[nodiscard]] std::size_t dispatch(
      const ReceivedCanFrame& frame) noexcept;

  [[nodiscard]] CanStatus validate_frame(const CanFrame& frame) const noexcept;
  void latch_fault(CanStatus status) noexcept;

  ICanDriver& driver_;
  CanChannelConfig active_config_{};
  CanCapabilities active_capabilities_{};
  bool owns_driver_lease_{false};
  bool dispatching_{false};
  bool stop_pending_{false};
  runtime::ConfigurationGate configuration_{};
  std::array<Subscription, kMaxSubscriptions> subscriptions_{};
  CanBusState state_{CanBusState::configuring};
  std::uint16_t next_generation_{1U};
};

}  // namespace ecu::core::v2::transport
