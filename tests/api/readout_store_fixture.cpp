#include "ecu/api/daf_sac_projection.hpp"
#include "ecu/api/linux_readout_store.hpp"
#include "ecu/dut_profiles/daf_sac/profile.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  namespace api = ecu::api::v1;
  namespace application = ecu::applications::daf_sac;
  namespace sac = ecu::dut_profiles::daf_sac;
  // Explicitly synthetic test fixture, not evidence from a physical ECU.
  application::AppSnapshot snapshot{};
  snapshot.state = application::AppState::dtcs_ready;
  snapshot.status = application::AppStatus::ok;
  snapshot.profile_id = sac::profile_id(sac::CanBitrateProfile::k500k);
  snapshot.bitrate = 500000U;
  snapshot.dtcs_available = true;
  snapshot.dtc_count = 2U;
  snapshot.bench.configured = true;
  snapshot.bench.state = ecu::bench::BenchSessionState::ready;
  snapshot.bench.status = ecu::bench::BenchSessionStatus::ok;
  snapshot.bench.dut_profile_id = snapshot.profile_id;
  snapshot.bench.last_completed_operation_generation = 12U;
  sac::SacDtcList dtcs{};
  dtcs.valid = true;
  dtcs.status_availability = 0x8bU;
  dtcs.requested_mask = 0xffU;
  dtcs.count = 2U;
  dtcs.records[0] = {0x3A0002U, 0x08U};
  dtcs.records[1] = {0x08F9E2U, 0x8bU};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (now <= 0) return 1;
  const auto captured = api::capture_daf_sac_completed_readout(
      snapshot, dtcs, static_cast<std::uint64_t>(now));
  if (captured.status != api::ReadStatus::ok ||
      !api::publish_linux_readout(argv[1], captured.value))
    return 1;
  // Publish synthetic completed native parameters through the SAME secure
  // Linux store; this fixture never opens CAN or controls a real DUT.
  snapshot.state = application::AppState::parameters_ready;
  snapshot.dtcs_available = false;
  snapshot.dtc_count = 0U;
  snapshot.voltage_available = true;
  snapshot.pressure_received = true;
  sac::SacVoltage voltage{};
  voltage.valid = true;
  voltage.permanent_v = 27.9F;
  voltage.ignition_v = 27.9F;
  sac::SacPressure pressure{};
  pressure.received = true;
  const auto parameter_snapshot =
      api::capture_daf_sac_completed_parameters(
          snapshot, voltage, pressure, static_cast<std::uint64_t>(now));
  if (parameter_snapshot.status != api::ReadStatus::ok ||
      !api::publish_linux_sac_parameters(
          argv[1], parameter_snapshot.value))
    return 1;
  std::cout << "ECU_API_SYNTHETIC_FIXTURE=PASS\n";
  return 0;
}
