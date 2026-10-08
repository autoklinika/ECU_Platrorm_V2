#include "ecu/core/runtime/event_bus.hpp"

namespace ecu::core::runtime {

EventBus::EventBus(
    const time::IMonotonicClock& clock) noexcept
    : clock_(clock) {}

EventSubscriptionStatus EventBus::subscribe(
    const EventTypeId type,
    IEventSink& sink) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  Subscription* free_slot = nullptr;

  for (auto& subscription : subscriptions_) {
    if (subscription.sink == &sink &&
        subscription.type == type) {
      return EventSubscriptionStatus::already_subscribed;
    }

    if (subscription.sink == nullptr && free_slot == nullptr) {
      free_slot = &subscription;
    }
  }

  if (free_slot == nullptr) {
    return EventSubscriptionStatus::capacity_exhausted;
  }

  free_slot->type = type;
  free_slot->sink = &sink;
  return EventSubscriptionStatus::subscribed;
}

bool EventBus::unsubscribe(
    const EventTypeId type,
    IEventSink& sink) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (auto& subscription : subscriptions_) {
    if (subscription.sink == &sink &&
        subscription.type == type) {
      subscription = Subscription{};
      return true;
    }
  }

  return false;
}

void EventBus::publish(const EventView& event) noexcept {
  std::array<IEventSink*, kMaxSubscriptions> targets{};
  std::size_t target_count = 0U;
  EventView published = event;

  {
    std::lock_guard<std::mutex> lock{mutex_};

    ++sequence_;
    if (sequence_ == 0U) {
      ++sequence_;
    }

    published.header.sequence = sequence_;
    published.header.timestamp = clock_.now();

    for (const auto& subscription : subscriptions_) {
      if (subscription.sink != nullptr &&
          (subscription.type == kAllEventTypes ||
           subscription.type == event.header.type)) {
        targets[target_count++] = subscription.sink;
      }
    }
  }

  for (std::size_t i = 0U; i < target_count; ++i) {
    targets[i]->publish(published);
  }
}

EventSequence EventBus::last_sequence() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  return sequence_;
}

std::size_t EventBus::subscription_count() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  std::size_t count = 0U;
  for (const auto& subscription : subscriptions_) {
    if (subscription.sink != nullptr) {
      ++count;
    }
  }
  return count;
}

}  // namespace ecu::core::runtime
