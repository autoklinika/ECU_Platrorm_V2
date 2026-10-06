#pragma once

#include "ecu/core/time/monotonic_clock.hpp"
#include "ecu/core/transport/i_can_interface.hpp"

#include <string>

namespace ecu::platform::linux::socketcan {

class SocketCanAdapter final : public core::transport::ICanInterface {
 public:
  SocketCanAdapter(
      std::string interface_name,
      const core::time::IMonotonicClock& clock);

  ~SocketCanAdapter() override;

  SocketCanAdapter(const SocketCanAdapter&) = delete;
  SocketCanAdapter& operator=(const SocketCanAdapter&) = delete;
  SocketCanAdapter(SocketCanAdapter&&) = delete;
  SocketCanAdapter& operator=(SocketCanAdapter&&) = delete;

  [[nodiscard]] core::transport::CanCapabilities capabilities()
      const noexcept override;

  core::transport::CanStatus open(
      const core::transport::CanChannelConfig& config) noexcept override;
  void close() noexcept override;
  [[nodiscard]] bool is_open() const noexcept override;

  core::transport::CanStatus send(
      const core::transport::CanFrame& frame) noexcept override;
  [[nodiscard]] core::transport::CanReceiveResult try_receive()
      noexcept override;

 private:
  std::string interface_name_;
  const core::time::IMonotonicClock& clock_;
  int socket_fd_{-1};
  core::transport::CanCapabilities active_capabilities_{};
  core::transport::CanChannelConfig active_config_{};
};

}  // namespace ecu::platform::linux::socketcan
