#include "ecu/platform/linux/socketcan/socketcan_adapter.hpp"

#include "ecu/platform/linux/socketcan/detail/socketcan_codec.hpp"
#include "ecu/platform/linux/socketcan/socketcan_link_info.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ecu::platform::linux::socketcan {
namespace {

using core::transport::CanChannelConfig;
using core::transport::CanStatus;

CanStatus map_write_error(const int error) noexcept {
  switch (error) {
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ENOBUFS:
      return CanStatus::would_block;
    case EMSGSIZE:
    case EINVAL:
      return CanStatus::invalid_argument;
    default:
      return CanStatus::io_error;
  }
}

CanStatus map_link_query_status(const LinkQueryStatus status) noexcept {
  switch (status) {
    case LinkQueryStatus::ok:
      return CanStatus::ok;
    case LinkQueryStatus::not_found:
    case LinkQueryStatus::not_can:
    case LinkQueryStatus::io_error:
      return CanStatus::io_error;
  }
  return CanStatus::io_error;
}

bool profile_matches(
    const SocketCanLinkInfo& link,
    const CanChannelConfig& config) noexcept {
  if (!link.up || link.nominal_bitrate != config.nominal_bitrate) {
    return false;
  }

  if (config.fd_enabled) {
    if (!link.fd_enabled || link.data_bitrate != config.data_bitrate) {
      return false;
    }
  }

  const bool wants_listen_only =
      config.mode == core::transport::CanMode::listen_only;
  return link.listen_only_enabled == wants_listen_only;
}

}  // namespace

SocketCanAdapter::SocketCanAdapter(
    std::string interface_name,
    const core::time::IMonotonicClock& clock)
    : interface_name_(std::move(interface_name)),
      clock_(clock) {}

SocketCanAdapter::~SocketCanAdapter() {
  close();
}

core::transport::CanCapabilities SocketCanAdapter::capabilities()
    const noexcept {
  const auto query = query_socketcan_link(interface_name_.c_str());
  if (query.status != LinkQueryStatus::ok) {
    return core::transport::CanCapabilities{
        false, false, false, false, 0U};
  }
  return query.info.capabilities;
}

CanStatus SocketCanAdapter::open(
    const CanChannelConfig& config) noexcept {
  close();

  if (!core::transport::is_valid_can_channel_config(config)) {
    return CanStatus::invalid_argument;
  }

  const auto query = query_socketcan_link(interface_name_.c_str());
  if (query.status != LinkQueryStatus::ok) {
    return map_link_query_status(query.status);
  }

  if (query.info.bus_off) {
    return CanStatus::bus_off;
  }

  if (!core::transport::capabilities_support(
          query.info.capabilities, config)) {
    return CanStatus::unsupported;
  }

  if (!profile_matches(query.info, config)) {
    return CanStatus::unsupported;
  }

  const unsigned int interface_index =
      if_nametoindex(interface_name_.c_str());
  if (interface_index == 0U) {
    return CanStatus::io_error;
  }

  const int fd =
      ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
  if (fd < 0) {
    return CanStatus::io_error;
  }

  if (config.fd_enabled) {
    const int enable = 1;
    if (::setsockopt(
            fd,
            SOL_CAN_RAW,
            CAN_RAW_FD_FRAMES,
            &enable,
            sizeof(enable)) < 0) {
      ::close(fd);
      return CanStatus::unsupported;
    }
  }

  const can_err_mask_t error_mask = CAN_ERR_BUSOFF;
  if (::setsockopt(
          fd,
          SOL_CAN_RAW,
          CAN_RAW_ERR_FILTER,
          &error_mask,
          sizeof(error_mask)) < 0) {
    ::close(fd);
    return CanStatus::io_error;
  }

  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = static_cast<int>(interface_index);

  if (::bind(
          fd,
          reinterpret_cast<const sockaddr*>(&address),
          sizeof(address)) < 0) {
    ::close(fd);
    return CanStatus::io_error;
  }

  socket_fd_ = fd;
  active_capabilities_ = query.info.capabilities;
  active_config_ = config;
  return CanStatus::ok;
}

void SocketCanAdapter::close() noexcept {
  if (socket_fd_ >= 0) {
    ::close(socket_fd_);
    socket_fd_ = -1;
  }

  active_capabilities_ = {};
  active_config_ = {};
}

bool SocketCanAdapter::is_open() const noexcept {
  return socket_fd_ >= 0;
}

CanStatus SocketCanAdapter::send(
    const core::transport::CanFrame& frame) noexcept {
  if (!is_open()) {
    return CanStatus::not_open;
  }

  if (active_config_.mode == core::transport::CanMode::listen_only) {
    return CanStatus::unsupported;
  }

  if (!core::transport::is_valid_can_frame(frame)) {
    return CanStatus::invalid_argument;
  }

  if (!core::transport::capabilities_support_frame(
          active_capabilities_, frame)) {
    return CanStatus::unsupported;
  }

  if (frame.format == core::transport::CanFrameFormat::fd) {
    if (!active_config_.fd_enabled) {
      return CanStatus::unsupported;
    }

    canfd_frame encoded{};
    const auto status = detail::encode_fd_frame(frame, encoded);
    if (status != CanStatus::ok) {
      return status;
    }

    const ssize_t written =
        ::write(socket_fd_, &encoded, CANFD_MTU);
    if (written == CANFD_MTU) {
      return CanStatus::ok;
    }
    return written < 0
               ? map_write_error(errno)
               : CanStatus::io_error;
  }

  can_frame encoded{};
  const auto status = detail::encode_classic_frame(frame, encoded);
  if (status != CanStatus::ok) {
    return status;
  }

  const ssize_t written =
      ::write(socket_fd_, &encoded, CAN_MTU);
  if (written == CAN_MTU) {
    return CanStatus::ok;
  }
  return written < 0
             ? map_write_error(errno)
             : CanStatus::io_error;
}

core::transport::CanReceiveResult SocketCanAdapter::try_receive()
    noexcept {
  core::transport::CanReceiveResult result{};

  if (!is_open()) {
    result.status = CanStatus::not_open;
    return result;
  }

  canfd_frame buffer{};
  const ssize_t received =
      ::read(socket_fd_, &buffer, sizeof(buffer));

  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      result.status = CanStatus::would_block;
    } else {
      result.status = CanStatus::io_error;
    }
    return result;
  }

  if (received == CAN_MTU) {
    const auto* classic =
        reinterpret_cast<const can_frame*>(&buffer);

    if ((classic->can_id & CAN_ERR_FLAG) != 0U) {
      result.status =
          (classic->can_id & CAN_ERR_BUSOFF) != 0U
              ? CanStatus::bus_off
              : CanStatus::io_error;
      return result;
    }

    const auto status = detail::decode_classic_frame(
        *classic, clock_.now(), result);
    result.status = status;
    return result;
  }

  if (received == CANFD_MTU) {
    const auto status = detail::decode_fd_frame(
        buffer, clock_.now(), result);
    result.status = status;
    return result;
  }

  result.status = CanStatus::io_error;
  return result;
}

}  // namespace ecu::platform::linux::socketcan
