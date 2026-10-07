#pragma once

#include "ecu/bench/host_service.hpp"
#include "ecu/bench/session.hpp"
#include "ecu/core_v2/protocol/uds/uds_client.hpp"
#include "ecu/core_v2/transport/can_bus_runtime.hpp"
#include "ecu/dut_profile/runtime.hpp"
#include "ecu/dut_profiles/daf_sac/identification_program.hpp"
#include "ecu/dut_profiles/daf_sac/profile.hpp"

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
};

inline constexpr std::array<OperationDescriptor, 6U> kOperationCatalog{{
    {AppOperation::identify, "identify", true, true},
    {AppOperation::read_dtc, "read_dtc", false, true},
    {AppOperation::clear_dtc, "clear_dtc", false, false},
    {AppOperation::live_parameters, "live_parameters", false, true},
    {AppOperation::actuator_test, "actuator_test", false, false},
    {AppOperation::program_ecu, "program_ecu", false, false},
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
  [[nodiscard]] bool close_bus() noexcept;

  ecu::dut_profile::ResolvedDutSessionPlan plan_{};
  ecu::core::v2::transport::CanBusRuntime& bus_;
  ecu::dut_profile::DutProfileSessionEndpoint& profile_endpoint_;
  ecu::dut_profiles::daf_sac::IdentificationProgram& program_;
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

  [[nodiscard]] bool configure(
      ecu::bench::BenchHostServiceConfig host_config) noexcept;
  [[nodiscard]] AppStatus identify() noexcept;
  [[nodiscard]] AppStatus service() noexcept;
  [[nodiscard]] AppStatus stop() noexcept;
  [[nodiscard]] AppStatus recover() noexcept;

  [[nodiscard]] AppSnapshot snapshot() const noexcept;
  // Full VIN is retained in memory only. GUI/API must authorize access.
  [[nodiscard]] const ecu::dut_profiles::daf_sac::IdentificationResult&
  identification() const noexcept;

 private:
  [[nodiscard]] AppStatus fail(
      const ecu::bench::BenchHostServiceResult& result) noexcept;

  ecu::dut_profile::ResolvedDutSessionPlan plan_{};
  BenchEndpoint& endpoint_;
  ecu::bench::BenchSession& session_;
  ecu::bench::BenchSessionHostRuntime& host_;
  ecu::dut_profiles::daf_sac::IdentificationProgram& program_;
  const ecu::core::v2::time::IMonotonicClock& clock_;
  ecu::core::v2::time::MonotonicTime deadline_{0};
  ecu::dut_profiles::daf_sac::IdentificationResult record_{};
  AppState state_{AppState::unconfigured};
  AppStatus status_{AppStatus::invalid_state};
  bool record_valid_{false};
};

}  // namespace ecu::applications::daf_sac
