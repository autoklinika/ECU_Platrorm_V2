#pragma once

#include "ecu/bench/host_service.hpp"
#include "ecu/bench/session.hpp"
#include "ecu/core_v2/protocol/uds/uds_client.hpp"
#include "ecu/core_v2/transport/can_bus_runtime.hpp"
#include "ecu/dut_profile/runtime.hpp"
#include "ecu/dut_profiles/daf_sac/identification_program.hpp"
#include "ecu/dut_profiles/daf_sac/profile.hpp"
#include "ecu/dut_profiles/daf_sac/service_program.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ecu::applications::daf_sac {

// Stage 4.0: the application layer is a consumer of frozen Core V2, Bench,
// and DUT Profile contracts. No Linux, SocketCAN, GPIO, or GUI dependencies.
enum class AppOperation : std::uint8_t {
  identify,
  read_dtc,
  clear_dtc,
  live_parameters,
  actuator_test,
  program_ecu,
};

struct OperationDescriptor {
  AppOperation operation{AppOperation::identify};
  const char* key{nullptr};
  bool available{false};
  bool read_only{true};
  bool physically_validated{false};
};

inline constexpr std::array<OperationDescriptor, 6U> kOperationCatalog{{
    {AppOperation::identify, "identify", true, true, true},
    // Physically read via 19 02 FF on both 250k and 500k SAC DUTs.
    {AppOperation::read_dtc, "read_dtc", true, true, true},
    {AppOperation::clear_dtc, "clear_dtc", true, false, false},
    {AppOperation::live_parameters, "live_parameters", true, true, false},
    {AppOperation::actuator_test, "actuator_test", false, false, false},
    {AppOperation::program_ecu, "program_ecu", false, false, false},
}};

[[nodiscard]] constexpr bool operation_available(
    const AppOperation operation) noexcept {
  for (const auto& item : kOperationCatalog) {
    if (item.operation == operation) {
      return item.available;
    }
  }
  return false;
}

// This is a SAC-specific composition adapter, not a new Bench Runtime.
// It lets Bench acquire the CAN resource before the CAN bus is opened;
// Bench safe shutdown always drives the DUT endpoint and closes the bus.
class BenchEndpoint final : public ecu::bench::IDutSessionEndpoint {
 public:
  static constexpr std::size_t kMaxPollFrames = 8U;

  BenchEndpoint(
      const ecu::dut_profile::ResolvedDutSessionPlan& plan,
      ecu::core::v2::transport::CanBusRuntime& bus,
      ecu::dut_profile::DutProfileSessionEndpoint& profile_endpoint,
      ecu::dut_profiles::daf_sac::IdentificationProgram& program,
      ecu::core::v2::transport::CanDriverExecutionContract driver_execution)
      noexcept;
  BenchEndpoint(
      const ecu::dut_profile::ResolvedDutSessionPlan& plan,
      ecu::core::v2::transport::CanBusRuntime& bus,
      ecu::dut_profile::DutProfileSessionEndpoint& profile_endpoint,
      ecu::dut_profiles::daf_sac::ServiceProgram& program,
      ecu::core::v2::transport::CanDriverExecutionContract driver_execution)
      noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ecu::bench::BenchComponentExecutionContract
  execution_contract() const noexcept override;
  [[nodiscard]] ecu::bench::BenchComponentStatus prepare() noexcept override;
  [[nodiscard]] ecu::bench::BenchComponentStatus activate() noexcept override;
  [[nodiscard]] ecu::bench::BenchComponentStatus service() noexcept override;
  [[nodiscard]] ecu::bench::BenchComponentStatus safe_stop() noexcept override;
  [[nodiscard]] ecu::bench::BenchComponentStatus stop() noexcept override;

  [[nodiscard]] ecu::core::v2::transport::CanStatus last_can_status() const noexcept;
  [[nodiscard]] ecu::core::v2::protocol::uds::UdsStatus last_uds_status() const noexcept;
  [[nodiscard]] ecu::core::v2::protocol::uds::UdsTransportFailure
  last_transport_failure() const noexcept;
  [[nodiscard]] std::uint8_t last_nrc() const noexcept;

 private:
  void capture_diagnostic_status() noexcept;
  void configure_execution(
      ecu::core::v2::transport::CanDriverExecutionContract driver_execution)
      noexcept;
  [[nodiscard]] bool close_bus() noexcept;

  ecu::dut_profile::ResolvedDutSessionPlan plan_{};
  ecu::core::v2::transport::CanBusRuntime& bus_;
  ecu::dut_profile::DutProfileSessionEndpoint& profile_endpoint_;
  ecu::dut_profiles::daf_sac::IdentificationProgram* program_{nullptr};
  ecu::dut_profiles::daf_sac::ServiceProgram* services_{nullptr};
  ecu::bench::BenchComponentExecutionContract execution_{};
  ecu::core::v2::transport::CanStatus last_can_status_{
      ecu::core::v2::transport::CanStatus::ok};
  ecu::core::v2::protocol::uds::UdsStatus last_uds_status_{
      ecu::core::v2::protocol::uds::UdsStatus::idle};
  ecu::core::v2::protocol::uds::UdsTransportFailure
      last_transport_failure_{
          ecu::core::v2::protocol::uds::UdsTransportFailure::none};
  std::uint8_t last_nrc_{0U};
  bool valid_{false};
  bool prepared_{false};
  bool active_{false};
  bool bus_started_{false};
};

enum class AppState : std::uint8_t {
  unconfigured,
  ready,
  identifying,
  identified,
  reading_parameters,
  parameters_ready,
  reading_dtcs,
  dtcs_ready,
  clearing_dtcs,
  dtcs_clear_acknowledged,
  faulted,
};

enum class AppStatus : std::uint8_t {
  ok,
  no_action,
  invalid_state,
  configuration_failed,
  resource_unavailable,
  runtime_fault,
  timeout,
  safe_shutdown_failed,
  confirmation_required,
  unsupported,
  clear_outcome_unknown,
};

// One-shot intent token for a destructive action. This is a guard against
// accidental execution, NOT a replacement for future API authentication.
struct ClearDtcChallenge {
  std::uint64_t sequence{0U};
  ecu::core::v2::domain::DutProfileId profile_id{0U};
  std::size_t inspected_dtc_count{0U};
};

struct AppSnapshot {
  static constexpr std::uint16_t kSchemaVersion = 1U;
  std::uint16_t schema_version{kSchemaVersion};
  AppState state{AppState::unconfigured};
  AppStatus status{AppStatus::invalid_state};
  ecu::core::v2::domain::DutProfileId profile_id{0U};
  std::uint32_t bitrate{0U};
  ecu::bench::BenchSessionSnapshot bench{};
  ecu::core::v2::transport::CanStatus can_status{
      ecu::core::v2::transport::CanStatus::ok};
  ecu::core::v2::protocol::uds::UdsStatus uds_status{
      ecu::core::v2::protocol::uds::UdsStatus::idle};
  ecu::core::v2::protocol::uds::UdsTransportFailure transport_failure{
      ecu::core::v2::protocol::uds::UdsTransportFailure::none};
  std::uint8_t nrc{0U};
  bool identification_available{false};
  bool voltage_available{false};
  bool pressure_received{false};
  bool pressure1_valid{false};
  bool pressure2_valid{false};
  bool dtcs_available{false};
  std::size_t dtc_count{0U};
  bool clear_acknowledged{false};
  // Privacy-safe GUI summary, never a complete VIN in diagnostic telemetry.
  std::array<char, 5U> vin_suffix{};
};

// Application command surface intended for a future authenticated WebGUI/API.
// No transport/power/driver operations are exposed as GUI commands.
class Application final {
 public:
  Application(
      const ecu::dut_profile::ResolvedDutSessionPlan& plan,
      BenchEndpoint& endpoint,
      ecu::bench::BenchSession& session,
      ecu::bench::BenchSessionHostRuntime& host,
      ecu::dut_profiles::daf_sac::IdentificationProgram& program,
      const ecu::core::v2::time::IMonotonicClock& clock) noexcept;
  Application(
      const ecu::dut_profile::ResolvedDutSessionPlan& plan,
      BenchEndpoint& endpoint,
      ecu::bench::BenchSession& session,
      ecu::bench::BenchSessionHostRuntime& host,
      ecu::dut_profiles::daf_sac::ServiceProgram& services,
      ecu::dut_profiles::daf_sac::PressureMonitor& pressure,
      const ecu::core::v2::time::IMonotonicClock& clock) noexcept;

  [[nodiscard]] bool configure(
      ecu::bench::BenchHostServiceConfig host_config) noexcept;
  [[nodiscard]] AppStatus identify() noexcept;
  [[nodiscard]] AppStatus read_parameters() noexcept;
  [[nodiscard]] AppStatus read_dtcs(std::uint8_t status_mask = 0xFFU) noexcept;
  [[nodiscard]] ClearDtcChallenge prepare_clear_dtcs() noexcept;
  [[nodiscard]] AppStatus clear_dtcs(
      const ClearDtcChallenge& challenge,
      bool explicitly_confirmed) noexcept;
  [[nodiscard]] AppStatus service() noexcept;
  [[nodiscard]] AppStatus stop() noexcept;
  [[nodiscard]] AppStatus recover() noexcept;

  [[nodiscard]] AppSnapshot snapshot() const noexcept;
  // Full VIN is retained in memory only. GUI/API must authorize access.
  [[nodiscard]] const ecu::dut_profiles::daf_sac::IdentificationResult&
  identification() const noexcept;
  [[nodiscard]] ecu::dut_profiles::daf_sac::SacVoltage voltage() const noexcept;
  [[nodiscard]] ecu::dut_profiles::daf_sac::SacPressure pressure() const noexcept;
  [[nodiscard]] const ecu::dut_profiles::daf_sac::SacDtcList& dtcs() const noexcept;

 private:
  [[nodiscard]] AppStatus begin_operation(AppState state) noexcept;
  void invalidate_clear_challenge() noexcept;
  [[nodiscard]] bool fresh_dtc_inventory() const noexcept;
  [[nodiscard]] bool operation_active() const noexcept;
  [[nodiscard]] AppStatus fail(
      const ecu::bench::BenchHostServiceResult& result) noexcept;

  ecu::dut_profile::ResolvedDutSessionPlan plan_{};
  BenchEndpoint& endpoint_;
  ecu::bench::BenchSession& session_;
  ecu::bench::BenchSessionHostRuntime& host_;
  ecu::dut_profiles::daf_sac::IdentificationProgram* program_{nullptr};
  ecu::dut_profiles::daf_sac::ServiceProgram* services_{nullptr};
  ecu::dut_profiles::daf_sac::PressureMonitor* pressure_{nullptr};
  const ecu::core::v2::time::IMonotonicClock& clock_;
  ecu::core::v2::time::MonotonicTime deadline_{0};
  ecu::core::v2::time::MonotonicTime pressure_wait_deadline_{0};
  ecu::dut_profiles::daf_sac::IdentificationResult record_{};
  ecu::dut_profiles::daf_sac::SacVoltage voltage_{};
  ecu::dut_profiles::daf_sac::SacPressure pressure_sample_{};
  ecu::dut_profiles::daf_sac::SacDtcList dtcs_{};
  ecu::core::v2::time::MonotonicTime last_dtc_read_at_{0};
  bool last_dtc_read_valid_{false};
  bool clear_acknowledged_{false};
  std::uint64_t next_clear_sequence_{0U};
  std::uint64_t armed_clear_sequence_{0U};
  AppState state_{AppState::unconfigured};
  AppStatus status_{AppStatus::invalid_state};
  bool record_valid_{false};
};

}  // namespace ecu::applications::daf_sac
