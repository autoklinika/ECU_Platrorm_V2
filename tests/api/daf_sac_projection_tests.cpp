#include "ecu/api/daf_sac_projection.hpp"
#include "ecu/api/router.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
namespace api = ecu::api::v1;
namespace app = ecu::applications::daf_sac;
namespace sac = ecu::dut_profiles::daf_sac;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "SAC_API_PROJECTION_TEST=FAIL " << message << '\n';
    std::exit(1);
  }
}

struct TestDtcModel final : api::IReadModel {
  app::AppSnapshot snapshot{};
  sac::SacDtcList list{};
  api::ReadResult<api::DtcInfo> dtcs() const override {
    return api::project_daf_sac_completed_dtc_read(snapshot, list);
  }
  api::ReadResult<api::CapabilitiesInfo> capabilities() const override {
    return api::project_daf_sac_capabilities(snapshot);
  }
};

}  // namespace

int main() {
  TestDtcModel synthetic{}; // TEST FIXTURE ONLY, never real ECU evidence.
  auto& snapshot = synthetic.snapshot;
  auto& list = synthetic.list;
  snapshot.profile_id = sac::profile_id(sac::CanBitrateProfile::k500k);
  snapshot.bitrate = 500000U;
  snapshot.state = app::AppState::dtcs_ready;
  snapshot.status = app::AppStatus::ok;
  snapshot.dtcs_available = true;
  snapshot.dtc_count = 2U;
  list.count = 2U;
  list.valid = true;
  list.status_availability = 0x8bU;
  list.requested_mask = 0xffU;
  list.records[0] = {0x3a0002U, 0x08U};
  list.records[1] = {0x08f9e2U, 0x8bU};

  const auto converted = synthetic.dtcs();
  require(converted.status == api::ReadStatus::ok, "typed-read-converts");
  require(converted.value.protocol == "uds", "protocol");
  require(converted.value.status_availability_mask == 0x8bU,
          "availability-mask");
  require(converted.value.requested_status_mask == 0xffU,
          "requested-mask");
  require(converted.value.entries.size() == 2U, "count");
  require(converted.value.entries[0].code == "3A0002", "uds-three-byte-code");
  require(converted.value.entries[1].code == "08F9E2", "leading-zero-preserved");
  require(converted.value.entries[1].status_mask == 0x8bU, "status-bitmask");

  const auto capabilities = synthetic.capabilities();
  require(capabilities.status == api::ReadStatus::ok, "capabilities-valid");
  require(capabilities.value.available_read_operations.size() == 2U,
          "only-physically-validated-reads");
  for (const auto operation : capabilities.value.available_read_operations)
    require(operation != api::ReadCapability::live_parameters,
            "no-unverified-live-parameters");

  api::Router router{synthetic, std::string(64U, 'a'), 8878U};
  const api::Request authorized{
      "GET", "/api/v1/dut/dtcs", "127.0.0.1:8878",
      "", "Bearer " + std::string(64U, 'a'), "", ""};
  const auto published = router.route(authorized);
  require(published.status == 200, "public-read-contract");
  require(published.body.find("\"status_availability_mask\":139") !=
              std::string::npos, "mask-serialized");
  require(published.body.find("08F9E2") != std::string::npos,
          "raw-three-byte-DTC-in-JSON");

  auto request = authorized;
  request.authorization.clear();
  require(router.route(request).status == 401, "auth-before-read");

  // Reject incomplete, contradictory or corrupted application-owned data.
  snapshot.dtcs_available = false;
  require(synthetic.dtcs().status == api::ReadStatus::backend_unavailable,
          "missing-DTC-read-not-empty");
  snapshot.dtcs_available = true;
  snapshot.state = app::AppState::reading_dtcs;
  require(synthetic.dtcs().status == api::ReadStatus::backend_unavailable,
          "in-progress-read-not-published");
  snapshot.state = app::AppState::dtcs_ready;
  snapshot.dtc_count = 1U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "count-mismatch");
  snapshot.dtc_count = 2U;
  snapshot.bench.cleanup_required = true;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "no-result-during-unsafe-cleanup");
  snapshot.bench.cleanup_required = false;
  snapshot.clear_acknowledged = true;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "clear-state-not-published");
  snapshot.clear_acknowledged = false;
  list.records[0].code = 0x1000000U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "outside-24-bit-code");
  list.records[0].code = 0x3a0002U;
  list.records[0].status = 0x10U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "unavailable-status-bit");
  list.records[0].status = 0x08U;
  list.records[1].code = 0x3a0002U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "duplicate-DTC");
  list.records[1].code = 0x08f9e2U;
  list.requested_mask = 0U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "invalid-requested-mask");
  list.requested_mask = 0xffU;
  snapshot.bitrate = 250000U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "wrong-500k-bitrate");
  snapshot.bitrate = 500000U;
  snapshot.schema_version = 99U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "unknown-application-schema");
  snapshot.schema_version = app::AppSnapshot::kSchemaVersion;
  snapshot.profile_id = 0U;
  require(synthetic.dtcs().status == api::ReadStatus::invalid_snapshot,
          "non-SAC-profile");
  require(synthetic.capabilities().status == api::ReadStatus::invalid_snapshot,
          "capabilities-untrusted-profile");

  // The 250k variant is supported by the same isolated adapter.
  snapshot.profile_id = sac::profile_id(sac::CanBitrateProfile::k250k);
  snapshot.bitrate = 250000U;
  require(synthetic.dtcs().status == api::ReadStatus::ok, "250k-profile");

  std::cout << "ECU_API_DAF_SAC_PROJECTION=PASS\n";
  return 0;
}
