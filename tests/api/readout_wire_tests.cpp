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
  std::cout << "ECU_API_READOUT_WIRE=PASS\n";
  return 0;
}
