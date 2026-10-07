#include "daf_sac_clear_operator.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace clear = ecu::tools::sac_clear;
namespace daf = ecu::dut_profiles::daf_sac;

int require(const bool condition, const char* reason) {
  if (!condition) {
    std::cerr << "FAIL: " << reason << '\n';
    return 1;
  }
  return 0;
}
}  // namespace

int main() {
  int failures = 0;
  failures += require(
      clear::confirmation_phrase(12U) == "KASUJ SAC 12 DTC" &&
          clear::confirmation_matches("KASUJ SAC 12 DTC", 12U) &&
          !clear::confirmation_matches("KASUJ SAC 11 DTC", 12U) &&
          !clear::confirmation_matches(" KASUJ SAC 12 DTC", 12U) &&
          !clear::confirmation_matches("KASUJ SAC 0 DTC", 0U),
      "exact count-bound confirmation; no empty-list confirmation");

  char directory_template[] = "/tmp/ecu-sac-clear-unit-XXXXXX";
  char* directory_path = ::mkdtemp(directory_template);
  if (directory_path == nullptr) {
    return 1;
  }
  const std::string directory{directory_path};
  daf::SacDtcList before{};
  before.valid = true;
  before.count = 2U;
  before.status_availability = 0x8BU;
  before.requested_mask = 0xFFU;
  before.records[0U] = {0x3A0002U, 0x0AU};
  before.records[1U] = {0xDFF7E9U, 0x8BU};

  std::string path;
  {
    clear::EvidenceFile evidence{};
    failures += require(evidence.create(
                            directory, before, 0xDAF00025U) &&
                            evidence.append(
                                "OPERATOR_CONFIRMATION=EXPLICIT_ONE_TIME\n"),
                        "durable backup created before any erase request");
    path = evidence.path();
    struct stat details {};
    failures += require(
        ::stat(path.c_str(), &details) == 0 &&
            S_ISREG(details.st_mode) &&
            (details.st_mode & 0077U) == 0U,
        "backup file is private; no group/world permissions");

    clear::EvidenceFile duplicate{};
    failures += require(!duplicate.create(
                            directory, before, 0xDAF00025U),
                        "unique backup cannot overwrite existing evidence");
  }

  std::ifstream stream{path};
  const std::string record{
      std::istreambuf_iterator<char>{stream},
      std::istreambuf_iterator<char>{}};
  failures += require(record.find(
                          "ECU_PLATFORM_V2_DAF_SAC_PRE_CLEAR_DTC=1") !=
                          std::string::npos &&
                          record.find("DTC=0x3A0002 STATUS=0x0A") !=
                          std::string::npos &&
                          record.find("DTC=0xDFF7E9 STATUS=0x8B") !=
                          std::string::npos &&
                          record.find("PRE_CLEAR_DTC_COUNT=2") !=
                          std::string::npos &&
                          record.find("OPERATOR_CONFIRMATION=EXPLICIT_ONE_TIME") !=
                          std::string::npos,
                      "full pre-clear raw evidence and audit trail are persisted");

  failures += require(::chmod(directory.c_str(), 0755) == 0,
                      "test chmod for insecure directory");
  {
    clear::EvidenceFile unsafe{};
    failures += require(!unsafe.create(
                            directory, before, 0xDAF00025U),
                        "insecure directory refuses to archive evidence");
  }
  failures += require(::chmod(directory.c_str(), 0700) == 0,
                      "restore directory permissions");

  const std::string alias = directory + "/not-a-directory";
  failures += require(::symlink(directory.c_str(), alias.c_str()) == 0,
                      "create test symlink");
  {
    clear::EvidenceFile unsafe{};
    failures += require(!unsafe.create(alias, before, 0xDAF00025U),
                        "symlinked evidence directory is refused");
  }

  before.valid = false;
  {
    clear::EvidenceFile invalid{};
    failures += require(!invalid.create(
                            directory, before, 0xDAF00025U),
                        "cannot save unvalidated DTC data");
  }

  (void)::unlink(alias.c_str());
  (void)::unlink(path.c_str());
  (void)::rmdir(directory.c_str());

  if (failures == 0) {
    std::cout << "DAF_SAC_CLEAR_OPERATOR_TESTS=PASS\n";
  }
  return failures == 0 ? 0 : 1;
}
