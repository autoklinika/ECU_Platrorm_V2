#include "ecu/api/projections.hpp"

namespace ecu::api::v1 {
namespace {
bool map_phase(ecu::bench::BenchSessionState state,
               BenchPhase& destination) noexcept {
  using State = ecu::bench::BenchSessionState;
  switch (state) {
    case State::unconfigured: destination = BenchPhase::unconfigured; return true;
    case State::ready: destination = BenchPhase::ready; return true;
    case State::starting: destination = BenchPhase::starting; return true;
    case State::running: destination = BenchPhase::running; return true;
    case State::stopping: destination = BenchPhase::stopping; return true;
    case State::recovering: destination = BenchPhase::recovering; return true;
    case State::faulted: destination = BenchPhase::faulted; return true;
  }
  return false;
}
bool map_kind(ecu::core::v2::domain::DutClass dut_class,
              DutKind& destination) noexcept {
  using Class = ecu::core::v2::domain::DutClass;
  switch (dut_class) {
    case Class::ecu: destination = DutKind::ecu; return true;
    case Class::actuator: destination = DutKind::actuator; return true;
    case Class::sensor: destination = DutKind::sensor; return true;
    case Class::gateway: destination = DutKind::gateway; return true;
    case Class::network_node: destination = DutKind::network_node; return true;
    case Class::other: destination = DutKind::other; return true;
  }
  return false;
}
}  // namespace

ReadResult<BenchInfo> project_bench(
    const ecu::bench::BenchSessionSnapshot& source) noexcept {
  if (source.schema_version !=
      ecu::bench::BenchSessionSnapshot::kSchemaVersion)
    return {ReadStatus::invalid_snapshot, {}};
  if (!source.configured) {
    return {ReadStatus::no_active_session, {}};
  }
  if (source.dut_profile_id == 0U)
    return {ReadStatus::invalid_snapshot, {}};
  BenchInfo data{};
  if (!map_phase(source.state, data.phase) ||
      data.phase == BenchPhase::unconfigured)
    return {ReadStatus::invalid_snapshot, {}};
  data.profile_id = source.dut_profile_id;
  data.revision = source.lifecycle_revision;
  data.configured = source.configured;
  data.cleanup_required = source.cleanup_required;
  return {ReadStatus::ok, data};
}
ReadResult<DutInfo> project_selected_dut(
    const ecu::dut_profile::DutProfileDefinition& selected) {
  if (ecu::dut_profile::validate_profile_definition(selected) !=
      ecu::dut_profile::ProfileValidationStatus::valid)
    return {ReadStatus::invalid_snapshot, {}};
  DutInfo result{};
  result.profile_id = selected.dut.profile_id;
  if (!map_kind(selected.dut.dut_class, result.kind))
    return {ReadStatus::invalid_snapshot, {}};
  // DutDescriptor has no vendor/model; do not infer physical presence.
  return {ReadStatus::ok, result};
}

}  // namespace ecu::api::v1
