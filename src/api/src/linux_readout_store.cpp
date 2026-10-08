#include "ecu/api/linux_readout_store.hpp"
#include "ecu/api/readout_wire.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace ecu::api::v1 {
namespace {
constexpr const char* kDtcName = "dtc-latest.v1";
constexpr const char* kParametersName = "sac-parameters-latest.v1";

// Open each component without following symlinks, including the parents. This
// avoids replacing the trusted producer directory via an ancestor symlink.
int open_directory(const std::string& path) noexcept {
  if (path.empty() || path.front() != '/' || path.size() > 512U)
    return -1;
  int dir = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir < 0) return -1;

  std::size_t pos = 1U;
  while (pos < path.size()) {
    const auto slash = path.find('/', pos);
    const auto end = slash == std::string::npos ? path.size() : slash;
    const std::string_view part(path.data() + pos, end - pos);
    if (part.empty() || part == "." || part == ".." || part.size() > 255U) {
      ::close(dir);
      return -1;
    }
    const std::string name{part};
    const int next = ::openat(
        dir, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(dir);
    if (next < 0) return -1;
    dir = next;
    if (slash == std::string::npos) break;
    pos = slash + 1U;
  }
  return dir;
}

bool trusted_directory(int descriptor, uid_t owner, gid_t group) noexcept {
  struct stat stat_info{};
  if (::fstat(descriptor, &stat_info) != 0 ||
      !S_ISDIR(stat_info.st_mode))
    return false;
  // World access and group modification are always prohibited. The setgid
  // bit makes atomically published files inherit the read-only reader group.
  return stat_info.st_uid == owner && stat_info.st_gid == group &&
         (stat_info.st_mode & 0777) == 0750 &&
         (stat_info.st_mode & S_ISGID) != 0;
}

bool trusted_file(int descriptor, uid_t owner, gid_t group,
                  std::size_t& length) noexcept {
  struct stat stat_info{};
  if (::fstat(descriptor, &stat_info) != 0 ||
      !S_ISREG(stat_info.st_mode) ||
      stat_info.st_uid != owner || stat_info.st_gid != group ||
      (stat_info.st_mode & 07777) != 0640 ||
      stat_info.st_nlink != 1 || stat_info.st_size <= 0 ||
      stat_info.st_size > static_cast<off_t>(kMaxReadoutBytes))
    return false;
  length = static_cast<std::size_t>(stat_info.st_size);
  return true;
}

bool write_all(int fd, std::string_view bytes) noexcept {
  std::size_t position = 0U;
  while (position < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + position,
                               bytes.size() - position);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    position += static_cast<std::size_t>(count);
  }
  return true;
}

}  // namespace

namespace {
// All completion records use EXACTLY the existing secure file boundary:
// nofollow ancestors, strict owner/mode, one-link inode, bounded bytes and
// freshness/future-clock validation. File names are fixed code constants.
template <typename T, typename Decoder>
ReadResult<T> load_named_record(
    const std::string& directory, const uid_t producer_uid,
    const gid_t reader_gid, const std::uint64_t now_unix_ms,
    const std::uint64_t maximum_age_ms,
    const char* final_name, Decoder decode) {
  if (now_unix_ms == 0U || maximum_age_ms == 0U)
    return {ReadStatus::backend_unavailable, {}};

  const int dir = open_directory(directory);
  if (dir < 0) return {ReadStatus::backend_unavailable, {}};
  if (!trusted_directory(dir, producer_uid, reader_gid)) {
    ::close(dir);
    return {ReadStatus::invalid_snapshot, {}};
  }
  const int file = ::openat(dir, final_name,
                            O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  const int open_error = errno;
  ::close(dir);
  if (file < 0) {
    if (open_error == ENOENT)
      return {ReadStatus::backend_unavailable, {}};
    return {ReadStatus::invalid_snapshot, {}};
  }
  std::size_t length = 0U;
  if (!trusted_file(file, producer_uid, reader_gid, length)) {
    ::close(file);
    return {ReadStatus::invalid_snapshot, {}};
  }
  std::string bytes(length, '\0');
  std::size_t total = 0U;
  while (total < length) {
    const auto n = ::read(file, bytes.data() + total, length - total);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      ::close(file);
      return {ReadStatus::invalid_snapshot, {}};
    }
    total += static_cast<std::size_t>(n);
  }
  // Fail closed if the producer modified the inode in place (it should
  // never do so; publication must use rename of a new immutable inode).
  std::size_t still_length = 0U;
  const bool unchanged =
      trusted_file(file, producer_uid, reader_gid, still_length) &&
      still_length == length;
  ::close(file);
  if (!unchanged) return {ReadStatus::invalid_snapshot, {}};

  auto parsed = decode(bytes);
  if (parsed.status != ReadStatus::ok) return parsed;

  constexpr std::uint64_t kMaxClockSkewMs = 2000U;
  const auto captured = parsed.value.captured_at_unix_ms;
  if (captured > now_unix_ms &&
      captured - now_unix_ms > kMaxClockSkewMs)
    return {ReadStatus::invalid_snapshot, {}};
  if (now_unix_ms > captured && now_unix_ms - captured > maximum_age_ms)
    return {ReadStatus::expired_readout, {}};
  return parsed;
}

bool publish_named_bytes(const std::string& directory,
                         const std::string_view encoded,
                         const char* final_name,
                         const char* temp_prefix) {
  if (encoded.empty() || encoded.size() > kMaxReadoutBytes) return false;
  const int dir = open_directory(directory);
  if (dir < 0) return false;
  struct stat directory_info{};
  if (::fstat(dir, &directory_info) != 0 ||
      !trusted_directory(dir, ::geteuid(), directory_info.st_gid)) {
    ::close(dir);
    return false;
  }
  const auto temp_name =
      std::string{temp_prefix} + std::to_string(::getpid()) + ".tmp";
  const int file = ::openat(dir, temp_name.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                           0600);
  if (file < 0) {
    ::close(dir);
    return false;
  }
  const bool written = ::fchmod(file, 0640) == 0 &&
                       write_all(file, encoded) &&
                       ::fsync(file) == 0;
  const bool closed = ::close(file) == 0;
  bool committed = false;
  if (written && closed) {
    committed = ::renameat(dir, temp_name.c_str(), dir, final_name) == 0;
    if (committed && ::fsync(dir) != 0) committed = false;
  }
  if (!committed) {
    (void)::unlinkat(dir, temp_name.c_str(), 0);
  }
  ::close(dir);
  return committed;
}

}  // namespace

ReadResult<CompletedDtcReadout> load_linux_readout(
    const std::string& directory, const uid_t producer_uid,
    const gid_t reader_gid, const std::uint64_t now_unix_ms,
    const std::uint64_t maximum_age_ms) {
  return load_named_record<CompletedDtcReadout>(
      directory, producer_uid, reader_gid, now_unix_ms, maximum_age_ms,
      kDtcName, decode_completed_readout);
}

ReadResult<CompletedSacParameters> load_linux_sac_parameters(
    const std::string& directory, const uid_t producer_uid,
    const gid_t reader_gid, const std::uint64_t now_unix_ms,
    const std::uint64_t maximum_age_ms) {
  return load_named_record<CompletedSacParameters>(
      directory, producer_uid, reader_gid, now_unix_ms, maximum_age_ms,
      kParametersName, decode_completed_sac_parameters);
}

bool publish_linux_readout(const std::string& directory,
                           const CompletedDtcReadout& record) {
  std::string encoded;
  if (!encode_completed_readout(record, encoded)) return false;
  return publish_named_bytes(directory, encoded, kDtcName, ".dtc-latest.");
}

bool publish_linux_sac_parameters(const std::string& directory,
                                  const CompletedSacParameters& record) {
  std::string encoded;
  if (!encode_completed_sac_parameters(record, encoded)) return false;
  return publish_named_bytes(
      directory, encoded, kParametersName, ".sac-parameters-latest.");
}

}  // namespace ecu::api::v1
