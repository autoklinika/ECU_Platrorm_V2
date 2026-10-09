#pragma once
#include "ecu/api/read_model.hpp"
#include "ecu/bench/session.hpp"
#include "ecu/dut_profile/profile.hpp"

namespace ecu::api::v1 {
[[nodiscard]] ReadResult<BenchInfo> project_bench(
    const ecu::bench::BenchSessionSnapshot& source) noexcept;
[[nodiscard]] ReadResult<DutInfo> project_selected_dut(
    const ecu::dut_profile::DutProfileDefinition& selected);
}  // namespace ecu::api::v1
