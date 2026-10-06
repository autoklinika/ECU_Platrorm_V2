#include "ecu/core/runtime/resource_manager.hpp"

namespace ecu::core::runtime {

ResourceAcquireResult ResourceManager::acquire(
    const ResourceKey resource,
    const ResourceOwnerId owner) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (owner == 0U) {
    return ResourceAcquireResult{
        ResourceAcquireStatus::invalid_argument,
        {}};
  }

  if (auto* existing = find_entry(resource)) {
    if (existing->owner == owner) {
      return ResourceAcquireResult{
          ResourceAcquireStatus::already_owned,
          ResourceLease{
              existing->resource,
              existing->owner,
              existing->generation}};
    }

    return ResourceAcquireResult{
        ResourceAcquireStatus::busy,
        {}};
  }

  auto* free_entry = find_free_entry();
  if (free_entry == nullptr) {
    return ResourceAcquireResult{
        ResourceAcquireStatus::capacity_exhausted,
        {}};
  }

  free_entry->used = true;
  free_entry->resource = resource;
  free_entry->owner = owner;
  free_entry->generation = next_generation();

  return ResourceAcquireResult{
      ResourceAcquireStatus::acquired,
      ResourceLease{
          free_entry->resource,
          free_entry->owner,
          free_entry->generation}};
}

ResourceReleaseStatus ResourceManager::release(
    const ResourceLease& lease) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (!lease.valid()) {
    return ResourceReleaseStatus::invalid_lease;
  }

  auto* entry = find_entry(lease.resource);
  if (entry == nullptr) {
    return ResourceReleaseStatus::stale_lease;
  }

  if (entry->owner != lease.owner) {
    return ResourceReleaseStatus::not_owner;
  }

  if (entry->generation != lease.generation) {
    return ResourceReleaseStatus::stale_lease;
  }

  *entry = Entry{};
  return ResourceReleaseStatus::released;
}

bool ResourceManager::owns(
    const ResourceKey resource,
    const ResourceOwnerId owner) const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  const auto* entry = find_entry(resource);
  return entry != nullptr && entry->owner == owner;
}

ResourceOwnerId ResourceManager::current_owner(
    const ResourceKey resource) const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  const auto* entry = find_entry(resource);
  return entry == nullptr ? 0U : entry->owner;
}

std::size_t ResourceManager::active_count() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  std::size_t count = 0U;
  for (const auto& entry : entries_) {
    if (entry.used) {
      ++count;
    }
  }

  return count;
}

ResourceManager::Entry* ResourceManager::find_entry(
    const ResourceKey resource) noexcept {
  for (auto& entry : entries_) {
    if (entry.used && entry.resource == resource) {
      return &entry;
    }
  }

  return nullptr;
}

const ResourceManager::Entry* ResourceManager::find_entry(
    const ResourceKey resource) const noexcept {
  for (const auto& entry : entries_) {
    if (entry.used && entry.resource == resource) {
      return &entry;
    }
  }

  return nullptr;
}

ResourceManager::Entry* ResourceManager::find_free_entry() noexcept {
  for (auto& entry : entries_) {
    if (!entry.used) {
      return &entry;
    }
  }

  return nullptr;
}

ResourceGeneration ResourceManager::next_generation() noexcept {
  ++generation_counter_;
  if (generation_counter_ == 0U) {
    ++generation_counter_;
  }
  return generation_counter_;
}

}  // namespace ecu::core::runtime
