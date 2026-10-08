#include "ecu/api/readout_wire.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ecu::api::v1;

namespace {
void require(bool ok, const char* label) {
  if (!ok) {
    std::cerr << "READOUT_WIRE_TEST=FAIL " << label << '\n';
    std::exit(1);
  }
}
void bad(const std::string& bytes) {
  require(decode_completed_readout(bytes).status ==
              ReadStatus::invalid_snapshot, "malformed packet rejected");
}
}  // namespace

int main() {
  CompletedDtcReadout input{};
  input.profile_id = 0xDAF00050U;
  input.captured_at_unix_ms = 1780934400000ULL;
  input.completed_generation = 5U;
  input.dtcs.protocol = "uds";
  input.dtcs.status_availability_mask = 0x8bU;
  input.dtcs.requested_status_mask = 0xffU;
  input.dtcs.entries = {{"3A0002", 0x08U}, {"08F9E2", 0x8bU}};
  std::string packet;
  require(encode_completed_readout(input, packet), "encoder");
  require(packet.size() < kMaxReadoutBytes, "bounded");
  require(packet.find("ECU_COMPLETED_DTC_V1\n") == 0U, "wire version");
  const auto parsed = decode_completed_readout(packet);
  require(parsed.status == ReadStatus::ok, "roundtrip");
  require(parsed.value.profile_id == input.profile_id &&
              parsed.value.captured_at_unix_ms == input.captured_at_unix_ms &&
              parsed.value.completed_generation == input.completed_generation &&
              parsed.value.dtcs.entries.size() == 2U &&
              parsed.value.dtcs.entries[1].code == "08F9E2",
          "all typed fields preserved");

  bad(std::string(kMaxReadoutBytes + 1U, 'a'));
  bad(packet + "garbage");
  bad(packet.substr(0U, packet.size() - 1U));
  auto corrupted = packet;
  corrupted.replace(0U, 1U, "X");
  bad(corrupted);
  corrupted = packet;
  const auto marker = corrupted.find("profile_id=");
  require(marker != std::string::npos, "profile marker");
  corrupted.replace(marker, 10U, "profile_x=");
  bad(corrupted);
  corrupted = packet;
  const auto count = corrupted.find("entry_count=2");
  require(count != std::string::npos, "count marker");
  corrupted.replace(count, 13U, "entry_count=3");
  bad(corrupted);
  corrupted = packet;
  const auto code = corrupted.find("08F9E2");
  require(code != std::string::npos, "DTC marker");
  corrupted.replace(code, 6U, "08f9e2");
  bad(corrupted);
  corrupted = packet;
  const auto mask = corrupted.find("status_availability=8B");
  require(mask != std::string::npos, "status marker");
  corrupted.replace(mask + 20U, 2U, "GG");
  bad(corrupted);
  corrupted = packet;
  const auto duplicate = corrupted.find("08F9E2");
  corrupted.replace(duplicate, 6U, "3A0002");
  bad(corrupted);
  corrupted = packet;
  corrupted.replace(duplicate + 7U, 2U, "10");
  bad(corrupted);

  input.dtcs.entries = {{"3A0002", 0x08U}, {"3A0002", 0x08U}};
  require(!encode_completed_readout(input, packet), "no duplicate publish");
  input.dtcs.entries.clear();
  require(encode_completed_readout(input, packet), "zero DTC valid");
  require(decode_completed_readout(packet).status == ReadStatus::ok,
          "zero DTC readout is not missing backend");
  input.completed_generation = 0U;
  require(!encode_completed_readout(input, packet),
          "missing completion generation denied");
  CompletedSacParameters params{};
  params.profile_id = 0xDAF00050U;
  params.captured_at_unix_ms = 1791492382691ULL;
  params.completed_generation = 3U;
  params.permanent_decivolt = 279U;
  params.ignition_decivolt = 279U;
  params.pgn_feae_observed = true;
  require(encode_completed_sac_parameters(params, packet),
          "valid-parameter-publish");
  require(packet.find("ECU_COMPLETED_SAC_PARAMETERS_V1\n") == 0U &&
          packet.find("pressure1_centibar=NA\n") != std::string::npos &&
          packet.find("pressure2_centibar=NA\n") != std::string::npos,
          "unavailable-pressure-canonical");
  auto decoded = decode_completed_sac_parameters(packet);
  require(decoded.status == ReadStatus::ok &&
          decoded.value.permanent_decivolt == 279U &&
          !decoded.value.pressure1_valid &&
          decoded.value.pgn_feae_observed,
          "strict-parameter-roundtrip");
  auto broken = packet;
  const auto point = broken.find("pressure1_centibar=NA");
  require(point != std::string::npos, "pressure field exists");
  broken.replace(point, std::string{"pressure1_centibar=NA"}.size(), "pressure1_centibar=0");
  require(decode_completed_sac_parameters(broken).status ==
              ReadStatus::ok,
          "actual-zero-pressure-is-valid-only-when-explicit");
  broken = packet;
  broken.replace(point, std::string{"pressure1_centibar=NA"}.size(), "pressure1_centibar=-1");
  require(decode_completed_sac_parameters(broken).status ==
              ReadStatus::invalid_snapshot, "negative-pressure-rejected");
  broken = packet;
  broken.replace(point, std::string{"pressure1_centibar=NA"}.size(), "pressure1_centibar=9999");
  require(decode_completed_sac_parameters(broken).status ==
              ReadStatus::invalid_snapshot, "overflow-pressure-rejected");
  broken = packet;
  broken.replace(0U, 1U, "X");
  require(decode_completed_sac_parameters(broken).status ==
              ReadStatus::invalid_snapshot, "unknown-parameter-schema");
  require(decode_completed_sac_parameters(packet + "extra").status ==
              ReadStatus::invalid_snapshot, "trailing-content-rejected");
  broken = packet;
  const auto gen = broken.find("completed_generation=3");
  broken.replace(gen, std::string{"completed_generation=3"}.size(), "completed_generation=0");
  require(decode_completed_sac_parameters(broken).status ==
              ReadStatus::invalid_snapshot, "zero-generation-rejected");
  params.profile_id = 0xDEADBEEFU;
  require(!encode_completed_sac_parameters(params, packet),
          "reject-non-SAC-profile");
  params.profile_id = 0xDAF00050U;
  params.pressure1_valid = true;
  params.pressure1_centibar = 1208U;
  require(encode_completed_sac_parameters(params, packet),
          "measured-pressure-encoded");
  decoded = decode_completed_sac_parameters(packet);
  require(decoded.status == ReadStatus::ok &&
          decoded.value.pressure1_valid &&
          decoded.value.pressure1_centibar == 1208U,
          "measured-pressure-roundtrip");
  params.pgn_feae_observed = false;
  require(!encode_completed_sac_parameters(params, packet),
          "no-pressure-measurement-without-pgn");
  std::cout << "ECU_API_READOUT_WIRE=PASS\n";
  return 0;
}
