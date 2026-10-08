#pragma once

#include "ecu/core/network/network_types.hpp"

#include <cstddef>

namespace ecu::core::network {

class IStreamChannel {
 public:
  virtual ~IStreamChannel() = default;

  virtual NetworkStatus open(
      NetworkInterfaceId interface_id) noexcept = 0;

  virtual NetworkStatus connect(
      const NetworkEndpoint& remote) noexcept = 0;

  virtual void close() noexcept = 0;

  [[nodiscard]] virtual bool is_open() const noexcept = 0;
  [[nodiscard]] virtual bool is_connected() const noexcept = 0;

  virtual NetworkStatus send(
      const std::byte* data,
      std::size_t length,
      std::size_t& sent) noexcept = 0;

  virtual NetworkStatus receive(
      std::byte* destination,
      std::size_t capacity,
      std::size_t& received) noexcept = 0;
};

}  // namespace ecu::core::network
