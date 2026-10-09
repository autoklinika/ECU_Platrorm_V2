#pragma once
#include "ecu/api/read_model.hpp"
#include <string>
#include <string_view>

namespace ecu::api::v1 {

// Strict bounded, versioned IPC body. Independent of HTTP, OS and hardware.
// Not a general-purpose JSON parser and contains no VIN or raw CAN payload.
inline constexpr std::size_t kMaxReadoutBytes = 8192U;
inline constexpr std::size_t kMaxReadoutDtcs = 128U;
[[nodiscard]] bool encode_completed_readout(
    const CompletedDtcReadout& value, std::string& destination);
[[nodiscard]] ReadResult<CompletedDtcReadout> decode_completed_readout(
    std::string_view source);

// Separate named file inside the SAME existing trusted readout directory.
// Explicit NA for unavailable pressure channels, never synthetic zero bar.
[[nodiscard]] bool encode_completed_sac_parameters(
    const CompletedSacParameters& value, std::string& destination);
[[nodiscard]] ReadResult<CompletedSacParameters>
decode_completed_sac_parameters(std::string_view source);

}  // namespace ecu::api::v1
