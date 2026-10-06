#include "ecu/core/runtime/state_registry.hpp"

namespace ecu::core::runtime {

StateRegistrationStatus StateRegistry::register_provider(
    IStateProvider& provider) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  const auto type = provider.state_type();
  if (type == 0U) {
    return StateRegistrationStatus::invalid_argument;
  }

  IStateProvider** free_slot = nullptr;

  for (auto& existing : providers_) {
    if (existing == &provider) {
      return StateRegistrationStatus::already_registered;
    }

    if (existing != nullptr &&
        existing->state_type() == type) {
      return StateRegistrationStatus::type_conflict;
    }

    if (existing == nullptr && free_slot == nullptr) {
      free_slot = &existing;
    }
  }

  if (free_slot == nullptr) {
    return StateRegistrationStatus::capacity_exhausted;
  }

  *free_slot = &provider;
  return StateRegistrationStatus::registered;
}

bool StateRegistry::unregister_provider(
    IStateProvider& provider) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (auto& existing : providers_) {
    if (existing == &provider) {
      existing = nullptr;
      return true;
    }
  }

  return false;
}

const IStateProvider* StateRegistry::find(
    const StateTypeId type) const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  for (const auto* provider : providers_) {
    if (provider != nullptr &&
        provider->state_type() == type) {
      return provider;
    }
  }

  return nullptr;
}

std::size_t StateRegistry::provider_count() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  std::size_t count = 0U;
  for (const auto* provider : providers_) {
    if (provider != nullptr) {
      ++count;
    }
  }
  return count;
}

}  // namespace ecu::core::runtime
