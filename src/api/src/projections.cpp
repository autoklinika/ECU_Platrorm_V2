#include "ecu/api/projections.hpp"

namespace ecu::api::v1 {
namespace {
BenchPhase phase(ecu::bench::BenchSessionState state) {
  using State = ecu::bench::BenchSessionState;
  switch (state) {
    case State::unconfigured: return BenchPhase::unconfigured;
    case State::ready: return BenchPhase::ready;
    case State::starting: return BenchPhase::starting;
    case State::running: return BenchPhase::running;
    case State::stopping: return BenchPhase::stopping;
    case State::recovering: return BenchPhase::recovering;
    case State::faulted: return BenchPhase::faulted;
  }
  return BenchPhase::faulted;
}
DutKind kind(ecu::core::v2::domain::DutClass dut_class) {
  using Class = ecu::core::v2::domain::DutClass;
  switch (dut_class) {
    case Class::ecu: return DutKind::ecu;
    case Class::actuator: return DutKind::actuator;
    case Class::sensor: return DutKind::sensor;
    case Class::gateway: return DutKind::gateway;
    case Class::network_node: return DutKind::network_node;
    case Class::other: return DutKind::other;
  }
  return DutKind::other;
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
  data.phase = phase(source.state);
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
  result.kind = kind(selected.dut.dut_class);
  // DutDescriptor has no vendor/model; do not infer physical presence.
  return {ReadStatus::ok, result};
}

}  // namespace ecu::api::v1
