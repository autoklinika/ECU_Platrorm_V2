#pragma once

#include "ecu/core/transport/can_types.hpp"

#include <cstdint>

namespace ecu::platform::linux::socketcan {

enum class LinkQueryStatus : std::uint8_t {
  ok,
  not_found,
  not_can,
  io_error,
};

struct SocketCanLinkInfo {
  bool up{false};
  bool bus_off{false};
  std::uint32_t nominal_bitrate{0};
  std::uint32_t data_bitrate{0};
  bool fd_enabled{false};
  bool listen_only_enabled{false};
  core::transport::CanCapabilities capabilities{};
};

struct LinkQueryResult {
  LinkQueryStatus status{LinkQueryStatus::io_error};
  SocketCanLinkInfo info{};
};

[[nodiscard]] LinkQueryResult query_socketcan_link(
    const char* interface_name) noexcept;

}  // namespace ecu::platform::linux::socketcan
