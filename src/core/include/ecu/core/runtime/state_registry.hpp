#pragma once

#include "ecu/core/runtime/state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ecu::core::runtime {

using StateTypeId = std::uint32_t;

enum class StateSnapshotStatus : std::uint8_t {
  ok,
  invalid_argument,
  buffer_too_small,
  unavailable,
};

class IStateProvider {
 public:
  virtual ~IStateProvider() = default;

  [[nodiscard]] virtual StateTypeId state_type() const noexcept = 0;
  [[nodiscard]] virtual StateRevision revision() const noexcept = 0;

  virtual StateSnapshotStatus snapshot(
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length,
      StateHeader& header) const noexcept = 0;
};

enum class StateRegistrationStatus : std::uint8_t {
  registered,
  already_registered,
  type_conflict,
  invalid_argument,
  capacity_exhausted,
};

class StateRegistry {
 public:
  static constexpr std::size_t kMaxProviders = 128U;

  [[nodiscard]] StateRegistrationStatus register_provider(
      IStateProvider& provider) noexcept;

  [[nodiscard]] bool unregister_provider(
      IStateProvider& provider) noexcept;

  [[nodiscard]] const IStateProvider* find(
      StateTypeId type) const noexcept;

  [[nodiscard]] std::size_t provider_count() const noexcept;

 private:
  mutable std::mutex mutex_{};
  std::array<IStateProvider*, kMaxProviders> providers_{};
};

}  // namespace ecu::core::runtime
