#include "ecu/api/daf_sac_projection.hpp"

#include "ecu/dut_profiles/daf_sac/profile.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace ecu::api::v1 {
namespace {
namespace sac = ecu::applications::daf_sac;
namespace profiles = ecu::dut_profiles::daf_sac;

bool valid_sac_profile(const sac::AppSnapshot& snapshot) noexcept {
  if (snapshot.schema_version != sac::AppSnapshot::kSchemaVersion)
    return false;
  const bool profile_250 =
      snapshot.profile_id == profiles::profile_id(profiles::CanBitrateProfile::k250k);
  const bool profile_500 =
      snapshot.profile_id == profiles::profile_id(profiles::CanBitrateProfile::k500k);
  return (profile_250 && snapshot.bitrate == 250000U) ||
         (profile_500 && snapshot.bitrate == 500000U);
}

std::string hex24(std::uint32_t code) {
  constexpr std::array<char, 16> digits{
      '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
  std::string value(6U, '0');
  for (int i = 5; i >= 0; --i) {
    value[static_cast<std::size_t>(i)] = digits[code & 0x0FU];
    code >>= 4U;
  }
  return value;
}
}  // namespace

ReadResult<DtcInfo> project_daf_sac_completed_dtc_read(
    const sac::AppSnapshot& snapshot,
    const profiles::SacDtcList& dtcs) {
  if (!valid_sac_profile(snapshot))
    return {ReadStatus::invalid_snapshot, {}};
  // A not-yet-completed or invalidated operation cannot be shown as an empty
  // DTC list. Retained data are not automatically current/live measurements.
  if (snapshot.state != sac::AppState::dtcs_ready ||
      snapshot.status != sac::AppStatus::ok ||
      !snapshot.dtcs_available || !dtcs.valid)
    return {ReadStatus::backend_unavailable, {}};

  if (snapshot.clear_acknowledged ||
      snapshot.bench.cleanup_required ||
      snapshot.bench.active_resource_count != 0U ||
      snapshot.dtc_count != dtcs.count ||
      dtcs.count > profiles::SacDtcList::kMaxEntries ||
      dtcs.requested_mask == 0U)
    return {ReadStatus::invalid_snapshot, {}};

  DtcInfo out{};
  out.protocol = "uds";
  out.status_availability_mask = dtcs.status_availability;
  out.requested_status_mask = dtcs.requested_mask;
  out.entries.reserve(dtcs.count);
  for (std::size_t i = 0U; i < dtcs.count; ++i) {
    const auto& entry = dtcs.records[i];
    if (entry.code > 0xFFFFFFU ||
        (entry.status & static_cast<std::uint8_t>(~dtcs.status_availability)) != 0U)
      return {ReadStatus::invalid_snapshot, {}};
    for (std::size_t j = 0U; j < i; ++j)
      if (dtcs.records[j].code == entry.code)
        return {ReadStatus::invalid_snapshot, {}};
    out.entries.push_back({hex24(entry.code), entry.status});
  }
  return {ReadStatus::ok, out};
}

ReadResult<CapabilitiesInfo> project_daf_sac_capabilities(
    const sac::AppSnapshot& snapshot) {
  if (!valid_sac_profile(snapshot))
    return {ReadStatus::invalid_snapshot, {}};
  CapabilitiesInfo out{};
  for (const auto& operation : sac::kOperationCatalog) {
    // Only approved read capabilities may reach a WebGUI/API observer.
    if (!operation.available || !operation.read_only ||
        !operation.physically_validated)
      continue;
    switch (operation.operation) {
      case sac::AppOperation::identify:
        out.available_read_operations.push_back(ReadCapability::identification);
        break;
      case sac::AppOperation::read_dtc:
        out.available_read_operations.push_back(ReadCapability::dtc_read);
        break;
      default:
        break;
    }
  }
  return {ReadStatus::ok, out};
}

}  // namespace ecu::api::v1
