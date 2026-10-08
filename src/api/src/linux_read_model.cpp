#include "ecu/api/linux_read_model.hpp"
#include "ecu/platform/linux/v2/socketcan_link_info.hpp"
#include <utility>

namespace ecu::api::v1 {

LinuxLinkReadModel::LinuxLinkReadModel(std::string interface_name)
    : interface_name_(std::move(interface_name)) {}

ReadResult<InterfacesInfo> LinuxLinkReadModel::interfaces() const {
  // Kernel netlink read only. No PF_CAN socket, CAN frames, configuration
  // mutations or Bench Agent permissions are needed.
  if (interface_name_.empty() || interface_name_.size() > 15U)
    return {ReadStatus::backend_unavailable, {}};
  const auto result = ecu::platform::linux::v2::query_socketcan_link(
      interface_name_.c_str(), 100);
  if (result.status !=
      ecu::platform::linux::v2::SocketCanLinkQueryStatus::ok)
    return {ReadStatus::backend_unavailable, {}};

  CanInterfaceInfo info{};
  info.name = interface_name_;
  info.up = result.info.up;
  info.fd_enabled = result.info.fd_enabled;
  info.listen_only = result.info.listen_only_enabled;
  info.bitrate = result.info.nominal_bitrate;
  info.data_bitrate = result.info.data_bitrate;
  return {ReadStatus::ok, {{std::move(info)}}};
}

}  // namespace ecu::api::v1
