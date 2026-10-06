#pragma once

#include "ecu/core/runtime/event.hpp"
#include "ecu/core/time/monotonic_clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ecu::core::runtime {

enum class EventSubscriptionStatus : std::uint8_t {
  subscribed,
  already_subscribed,
  invalid_argument,
  capacity_exhausted,
};

class EventBus final : public IEventSink {
 public:
  static constexpr std::size_t kMaxSubscriptions = 128U;
  static constexpr EventTypeId kAllEventTypes = 0U;

  explicit EventBus(
      const time::IMonotonicClock& clock) noexcept;

  [[nodiscard]] EventSubscriptionStatus subscribe(
      EventTypeId type,
      IEventSink& sink) noexcept;

  [[nodiscard]] bool unsubscribe(
      EventTypeId type,
      IEventSink& sink) noexcept;

  void publish(const EventView& event) noexcept override;

  [[nodiscard]] EventSequence last_sequence() const noexcept;
  [[nodiscard]] std::size_t subscription_count() const noexcept;

 private:
  struct Subscription {
    EventTypeId type{kAllEventTypes};
    IEventSink* sink{nullptr};
  };

  const time::IMonotonicClock& clock_;
  mutable std::mutex mutex_{};
  std::array<Subscription, kMaxSubscriptions> subscriptions_{};
  EventSequence sequence_{0U};
};

}  // namespace ecu::core::runtime
