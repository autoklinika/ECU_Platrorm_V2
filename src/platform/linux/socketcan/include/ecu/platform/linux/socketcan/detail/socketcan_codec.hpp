#pragma once

#include "ecu/core/transport/can_types.hpp"

#include <linux/can.h>

namespace ecu::platform::linux::socketcan::detail {

[[nodiscard]] core::transport::CanStatus encode_classic_frame(
    const core::transport::CanFrame& source,
    can_frame& target) noexcept;

[[nodiscard]] core::transport::CanStatus encode_fd_frame(
    const core::transport::CanFrame& source,
    canfd_frame& target) noexcept;

[[nodiscard]] core::transport::CanStatus decode_classic_frame(
    const can_frame& source,
    core::time::MonotonicTime timestamp,
    core::transport::CanReceiveResult& target) noexcept;

[[nodiscard]] core::transport::CanStatus decode_fd_frame(
    const canfd_frame& source,
    core::time::MonotonicTime timestamp,
    core::transport::CanReceiveResult& target) noexcept;

}  // namespace ecu::platform::linux::socketcan::detail
