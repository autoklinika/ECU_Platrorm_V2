#pragma once
#include "ecu/api/read_model.hpp"

#include <sys/types.h>
#include <cstdint>
#include <string>

namespace ecu::api::v1 {

// Linux filesystem boundary: directories owned by trusted producer,
// group-readable by a dedicated reader group. No device sockets.
[[nodiscard]] ReadResult<CompletedDtcReadout> load_linux_readout(
    const std::string& directory, uid_t producer_uid, gid_t reader_gid,
    std::uint64_t now_unix_ms, std::uint64_t maximum_age_ms);

// Called only by the trusted Application Layer operator process after a
// completed operation, never by HTTP. Replaces one file atomically.
[[nodiscard]] bool publish_linux_readout(
    const std::string& directory, const CompletedDtcReadout& record);

[[nodiscard]] ReadResult<CompletedSacParameters>
load_linux_sac_parameters(
    const std::string& directory, uid_t producer_uid, gid_t reader_gid,
    std::uint64_t now_unix_ms, std::uint64_t maximum_age_ms);
[[nodiscard]] bool publish_linux_sac_parameters(
    const std::string& directory, const CompletedSacParameters& record);

}  // namespace ecu::api::v1
