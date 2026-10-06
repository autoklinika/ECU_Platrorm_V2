#pragma once

#include "ecu/core/runtime/command.hpp"

#include <cstdint>

namespace ecu::core::security {

using PrincipalId = std::uint64_t;
using CapabilityId = std::uint32_t;

enum class AuthorizationDecision : std::uint8_t {
  allow,
  deny,
  confirmation_required,
};

struct AuthorizationContext {
  PrincipalId principal{0U};
  CapabilityId capability{0U};
  runtime::CorrelationId correlation_id{0U};
  bool authenticated{false};
  bool local_client{false};
};

class IAuthorizationPolicy {
 public:
  virtual ~IAuthorizationPolicy() = default;

  [[nodiscard]] virtual AuthorizationDecision authorize(
      const AuthorizationContext& context) const noexcept = 0;
};

}  // namespace ecu::core::security
