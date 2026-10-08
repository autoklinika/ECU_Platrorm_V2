#pragma once
#include "ecu/api/read_model.hpp"
#include <string>
namespace ecu::api::v1 {
class LinuxLinkReadModel final : public IReadModel {
 public:
  explicit LinuxLinkReadModel(std::string interface_name);
  [[nodiscard]] ReadResult<InterfacesInfo> interfaces() const override;
 private:
  std::string interface_name_;
};
}  // namespace ecu::api::v1
