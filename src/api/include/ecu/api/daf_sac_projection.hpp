#pragma once

#include "ecu/api/read_model.hpp"
#include "ecu/applications/daf_sac/application.hpp"
#include "ecu/dut_profiles/daf_sac/service_program.hpp"

namespace ecu::api::v1 {

// Optional, ECU-specific application adapter. Converts an immutable, completed
// Application Layer readout to the DUT-neutral API DTO without issuing any
// diagnostic operation. This does NOT implement cross-process publication.
[[nodiscard]] ReadResult<DtcInfo> project_daf_sac_completed_dtc_read(
    const ecu::applications::daf_sac::AppSnapshot& snapshot,
    const ecu::dut_profiles::daf_sac::SacDtcList& dtcs);

[[nodiscard]] ReadResult<CompletedDtcReadout> capture_daf_sac_completed_readout(
    const ecu::applications::daf_sac::AppSnapshot& snapshot,
    const ecu::dut_profiles::daf_sac::SacDtcList& dtcs,
    std::uint64_t captured_at_unix_ms);

[[nodiscard]] ReadResult<CapabilitiesInfo> project_daf_sac_capabilities(
    const ecu::applications::daf_sac::AppSnapshot& snapshot);

}  // namespace ecu::api::v1
