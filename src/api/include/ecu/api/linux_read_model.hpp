#pragma once
#include "ecu/api/read_model.hpp"
#include <string>
#include <sys/types.h>
namespace ecu::api::v1 {
class LinuxLinkReadModel final : public IReadModel {
 public:
  explicit LinuxLinkReadModel(std::string interface_name,
      std::string readout_directory = {}, uid_t producer_uid = 0,
      gid_t reader_gid = 0);
  [[nodiscard]] ReadResult<InterfacesInfo> interfaces() const override;
  [[nodiscard]] ReadResult<CompletedDtcReadout>
  latest_completed_dtcs() const override;
  [[nodiscard]] ReadResult<CompletedSacParameters>
  latest_completed_sac_parameters() const override;
 private:
  std::string interface_name_;
  std::string readout_directory_;
  uid_t producer_uid_{0};
  gid_t reader_gid_{0};
};
}  // namespace ecu::api::v1
