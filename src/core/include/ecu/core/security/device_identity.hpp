#pragma once

#include <cstddef>
#include <cstdint>

namespace ecu::core::security {

enum class SecurityStatus : std::uint8_t {
  ok,
  unsupported,
  unavailable,
  invalid_argument,
  buffer_too_small,
  verification_failed,
  internal_error,
};

class IDeviceIdentityProvider {
 public:
  virtual ~IDeviceIdentityProvider() = default;

  virtual SecurityStatus read_identity(
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length) noexcept = 0;

  virtual SecurityStatus create_attestation(
      const std::byte* challenge,
      std::size_t challenge_length,
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length) noexcept = 0;
};

}  // namespace ecu::core::security
