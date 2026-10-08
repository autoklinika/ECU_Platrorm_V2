#include "ecu/api/linux_read_model.hpp"
#include "ecu/api/linux_readout_store.hpp"
#include <chrono>
#include "ecu/platform/linux/v2/socketcan_link_info.hpp"
#include <utility>

namespace ecu::api::v1 {

LinuxLinkReadModel::LinuxLinkReadModel(
    std::string interface_name, std::string readout_directory,
    const uid_t producer_uid, const gid_t reader_gid)
    : interface_name_(std::move(interface_name)),
      readout_directory_(std::move(readout_directory)),
      producer_uid_(producer_uid), reader_gid_(reader_gid) {}

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
  info.bus_off = result.info.bus_off;
  info.fd_enabled = result.info.fd_enabled;
  info.listen_only = result.info.listen_only_enabled;
  info.bitrate = result.info.nominal_bitrate;
  info.data_bitrate = result.info.data_bitrate;
  return {ReadStatus::ok, {{std::move(info)}}};
}

ReadResult<CompletedDtcReadout>
LinuxLinkReadModel::latest_completed_dtcs() const {
  if (readout_directory_.empty() || producer_uid_ == 0)
    return {ReadStatus::backend_unavailable, {}};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (now <= 0)
    return {ReadStatus::backend_unavailable, {}};
  constexpr std::uint64_t kMaxReadoutAgeMs = 24U * 60U * 60U * 1000U;
  return load_linux_readout(readout_directory_, producer_uid_, reader_gid_,
                            static_cast<std::uint64_t>(now),
                            kMaxReadoutAgeMs);
}

}  // namespace ecu::api::v1
