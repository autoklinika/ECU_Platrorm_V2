#pragma once
#include "ecu/api/router.hpp"
#include <cstdint>
namespace ecu::api::v1 {
[[nodiscard]] int serve_loopback(const Router& router, std::uint16_t port);
}  // namespace ecu::api::v1
