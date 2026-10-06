#pragma once

#include "ecu/core/network/network_types.hpp"

#include <cstddef>
#include <cstdint>

namespace ecu::core::network {

struct DatagramOpenConfig {
  NetworkInterfaceId interface_id{0U};
  std::uint16_t local_port{0U};
  bool allow_broadcast{false};
};

class IDatagramChannel {
 public:
  virtual ~IDatagramChannel() = default;

  virtual NetworkStatus open(
      const DatagramOpenConfig& config) noexcept = 0;

  virtual void close() noexcept = 0;

  [[nodiscard]] virtual bool is_open() const noexcept = 0;

  virtual NetworkStatus send_to(
      const NetworkEndpoint& remote,
      const std::byte* data,
      std::size_t length,
      std::size_t& sent) noexcept = 0;

  virtual NetworkStatus receive_from(
      NetworkEndpoint& remote,
      std::byte* destination,
      std::size_t capacity,
      std::size_t& received) noexcept = 0;
};

}  // namespace ecu::core::network
