#pragma once

// Linux-only operator boundary. No dependency from Core V2, DUT Profile,
// or the portable ECU application to filesystem / interactive console I/O.
#include "ecu/dut_profiles/daf_sac/service_program.hpp"

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ecu::tools::sac_clear {

[[nodiscard]] inline std::string confirmation_phrase(
    const std::size_t count) {
  return "KASUJ SAC " + std::to_string(count) + " DTC";
}

[[nodiscard]] inline bool confirmation_matches(
    const std::string_view actual, const std::size_t count) {
  return count != 0U && actual == confirmation_phrase(count);
}

class EvidenceFile final {
 public:
  EvidenceFile() = default;
  EvidenceFile(const EvidenceFile&) = delete;
  EvidenceFile& operator=(const EvidenceFile&) = delete;

  ~EvidenceFile() {
    if (fd_ >= 0) {
      (void)::close(fd_);
    }
  }

  [[nodiscard]] bool create(
      const std::string& directory,
      const ecu::dut_profiles::daf_sac::SacDtcList& dtcs,
      const std::uint32_t profile_id) {
    if (fd_ >= 0 || !dtcs.valid || directory.empty() ||
        directory.front() != '/' || profile_id == 0U) {
      return false;
    }

    struct stat directory_stat {};
    if (::lstat(directory.c_str(), &directory_stat) != 0 ||
        !S_ISDIR(directory_stat.st_mode) ||
        directory_stat.st_uid != ::geteuid() ||
        (directory_stat.st_mode & 0077U) != 0U) {
      return false;
    }

    const auto unix_time = std::time(nullptr);
    if (unix_time == static_cast<std::time_t>(-1)) {
      return false;
    }
    path_ = directory + "/sac-dtc-" +
            std::to_string(static_cast<long long>(unix_time)) + "-" +
            std::to_string(static_cast<long long>(::getpid())) + ".txt";

    const int directory_fd = ::open(
        directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory_fd < 0) {
      return false;
    }
    fd_ = ::open(
        path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        S_IRUSR | S_IWUSR);
    if (fd_ < 0) {
      (void)::close(directory_fd);
      return false;
    }
    // Both the directory entry and file data must be durable before ANY
    // destructive request is enabled.
    const bool directory_synced = ::fsync(directory_fd) == 0;
    (void)::close(directory_fd);

    std::ostringstream text;
    text << "ECU_PLATFORM_V2_DAF_SAC_PRE_CLEAR_DTC=1\n"
         << "UTC_UNIX_TIME=" << static_cast<long long>(unix_time) << '\n'
         << "PROFILE_ID=0x" << std::hex << std::uppercase << profile_id
         << std::dec << '\n'
         << "DTC_STATUS_AVAILABILITY_MASK=0x" << std::hex
         << static_cast<unsigned int>(dtcs.status_availability)
         << std::dec << '\n'
         << "REQUESTED_DTC_STATUS_MASK=0x" << std::hex
         << static_cast<unsigned int>(dtcs.requested_mask)
         << std::dec << '\n'
         << "PRE_CLEAR_DTC_COUNT=" << dtcs.count << '\n';
    for (std::size_t i = 0U; i < dtcs.count; ++i) {
      text << "DTC=0x" << std::hex << std::setw(6) << std::setfill('0')
           << std::uppercase << dtcs.records[i].code
           << " STATUS=0x" << std::setw(2)
           << static_cast<unsigned int>(dtcs.records[i].status)
           << std::dec << '\n';
    }
    text << "PRE_CLEAR_DTC_EVIDENCE_COMPLETE=1\n";
    return directory_synced && append(text.str());
  }

  [[nodiscard]] bool append(const std::string_view data) noexcept {
    if (fd_ < 0) {
      return false;
    }
    std::size_t offset = 0U;
    while (offset < data.size()) {
      const auto written =
          ::write(fd_, data.data() + offset, data.size() - offset);
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written <= 0) {
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    return ::fsync(fd_) == 0;
  }

  [[nodiscard]] const std::string& path() const noexcept {
    return path_;
  }

 private:
  int fd_{-1};
  std::string path_{};
};

} // namespace ecu::tools::sac_clear
