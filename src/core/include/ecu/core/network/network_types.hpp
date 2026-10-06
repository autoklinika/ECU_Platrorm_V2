#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::core::network {

using NetworkInterfaceId = std::uint32_t;

enum class IpVersion : std::uint8_t {
  v4,
  v6,
};

struct IpAddress {
  IpVersion version{IpVersion::v4};
  std::array<std::byte, 16> bytes{};
};

struct NetworkEndpoint {
  IpAddress address{};
  std::uint16_t port{0U};
};

enum class NetworkStatus : std::uint8_t {
  ok,
  in_progress,
  would_block,
  not_open,
  not_connected,
  invalid_argument,
  buffer_too_small,
  timeout,
  io_error,
};

}  // namespace ecu::core::network
