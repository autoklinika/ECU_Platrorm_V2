#include "ecu/api/daf_sac_projection.hpp"

#include "ecu/dut_profiles/daf_sac/profile.hpp"

#include <array>
#include <cmath>
#include <limits>
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

  // The native AppSnapshot must be the result of a completed Bench operation
  // for exactly this DUT profile, never a still-running or mixed session.
  if (snapshot.clear_acknowledged ||
      snapshot.bench.schema_version !=
          ecu::bench::BenchSessionSnapshot::kSchemaVersion ||
      !snapshot.bench.configured ||
      snapshot.bench.state != ecu::bench::BenchSessionState::ready ||
      snapshot.bench.status != ecu::bench::BenchSessionStatus::ok ||
      snapshot.bench.dut_profile_id != snapshot.profile_id ||
      snapshot.bench.last_completed_operation_generation == 0U ||
      snapshot.bench.operation_generation != 0U ||
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

ReadResult<CompletedDtcReadout> capture_daf_sac_completed_readout(
    const sac::AppSnapshot& snapshot, const profiles::SacDtcList& dtcs,
    const std::uint64_t captured_at_unix_ms) {
  if (captured_at_unix_ms == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  auto converted = project_daf_sac_completed_dtc_read(snapshot, dtcs);
  if (converted.status != ReadStatus::ok)
    return {converted.status, {}};
  CompletedDtcReadout result{};
  result.captured_at_unix_ms = captured_at_unix_ms;
  result.profile_id = snapshot.profile_id;
  result.completed_generation =
      snapshot.bench.last_completed_operation_generation;
  result.dtcs = std::move(converted.value);
  return {ReadStatus::ok, std::move(result)};
}

ReadResult<CompletedSacParameters> capture_daf_sac_completed_parameters(
    const sac::AppSnapshot& snapshot,
    const profiles::SacVoltage& voltage,
    const profiles::SacPressure& pressure,
    const std::uint64_t captured_at_unix_ms) {
  if (!valid_sac_profile(snapshot) || captured_at_unix_ms == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  if (snapshot.state != sac::AppState::parameters_ready ||
      snapshot.status != sac::AppStatus::ok ||
      !snapshot.voltage_available || !voltage.valid)
    return {ReadStatus::backend_unavailable, {}};
  // Match the already-enforced DTC publisher lifecycle: no active CAN lease,
  // uncompleted operation, mismatched DUT profile or unsafe cleanup.
  if (snapshot.clear_acknowledged ||
      snapshot.bench.schema_version !=
          ecu::bench::BenchSessionSnapshot::kSchemaVersion ||
      !snapshot.bench.configured ||
      snapshot.bench.state != ecu::bench::BenchSessionState::ready ||
      snapshot.bench.status != ecu::bench::BenchSessionStatus::ok ||
      snapshot.bench.dut_profile_id != snapshot.profile_id ||
      snapshot.bench.last_completed_operation_generation == 0U ||
      snapshot.bench.operation_generation != 0U ||
      snapshot.bench.cleanup_required ||
      snapshot.bench.active_resource_count != 0U ||
      snapshot.pressure_received != pressure.received ||
      snapshot.pressure1_valid != pressure.pressure1_valid ||
      snapshot.pressure2_valid != pressure.pressure2_valid)
    return {ReadStatus::invalid_snapshot, {}};

  auto to_fixed = [](const float value, const float scale,
                     const std::uint16_t maximum, std::uint16_t& result) {
    if (!std::isfinite(value) || value < 0.0F ||
        value > static_cast<float>(maximum) / scale)
      return false;
    const auto count = std::lround(static_cast<double>(value) * scale);
    if (count < 0L || count > static_cast<long>(maximum))
      return false;
    result = static_cast<std::uint16_t>(count);
    return true;
  };

  CompletedSacParameters result{};
  result.captured_at_unix_ms = captured_at_unix_ms;
  result.profile_id = snapshot.profile_id;
  result.completed_generation =
      snapshot.bench.last_completed_operation_generation;
  if (!to_fixed(voltage.permanent_v, 10.0F, 600U,
                result.permanent_decivolt) ||
      !to_fixed(voltage.ignition_v, 10.0F, 600U,
                result.ignition_decivolt) ||
      (!pressure.received &&
       (pressure.pressure1_valid || pressure.pressure2_valid)))
    return {ReadStatus::invalid_snapshot, {}};

  result.pgn_feae_observed = pressure.received;
  result.pressure1_valid = pressure.pressure1_valid;
  result.pressure2_valid = pressure.pressure2_valid;
  if (result.pressure1_valid &&
      !to_fixed(pressure.pressure1_bar, 100.0F, 2024U,
                result.pressure1_centibar))
    return {ReadStatus::invalid_snapshot, {}};
  if (result.pressure2_valid &&
      !to_fixed(pressure.pressure2_bar, 100.0F, 2024U,
                result.pressure2_centibar))
    return {ReadStatus::invalid_snapshot, {}};
  return {ReadStatus::ok, result};
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
