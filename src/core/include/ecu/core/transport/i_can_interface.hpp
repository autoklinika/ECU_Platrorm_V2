#pragma once

#include "ecu/core/transport/can_types.hpp"

namespace ecu::core::transport {

class ICanInterface {
 public:
  virtual ~ICanInterface() = default;

  [[nodiscard]] virtual CanCapabilities capabilities() const noexcept = 0;

  virtual CanStatus open(const CanChannelConfig& config) noexcept = 0;
  virtual void close() noexcept = 0;
  [[nodiscard]] virtual bool is_open() const noexcept = 0;

  virtual CanStatus send(const CanFrame& frame) noexcept = 0;
  [[nodiscard]] virtual CanReceiveResult try_receive() noexcept = 0;
};

}  // namespace ecu::core::transport
