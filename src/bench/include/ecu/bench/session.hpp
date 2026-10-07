#pragma once

#include "ecu/core_v2/runtime/cancellation.hpp"
#include "ecu/core_v2/runtime/dut_registry.hpp"
#include "ecu/core_v2/runtime/resource_manager.hpp"
#include "ecu/core_v2/time/monotonic_clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::bench {

enum class BenchComponentStatus : std::uint8_t {
  ok,
  no_action,
  fault,
};

struct BenchComponentExecutionContract {
  ecu::core::v2::time::MonotonicDuration max_prepare_duration{0};
  ecu::core::v2::time::MonotonicDuration max_activate_duration{0};
  ecu::core::v2::time::MonotonicDuration max_service_duration{0};
  ecu::core::v2::time::MonotonicDuration max_safe_stop_duration{0};
  ecu::core::v2::time::MonotonicDuration max_stop_duration{0};
};

class IDutSessionEndpoint {
 public:
  [[nodiscard]] virtual BenchComponentExecutionContract
  execution_contract() const noexcept = 0;

  // prepare() may open transports/listeners but must not start active DUT
  // actuation. activate() enables the profile's running behavior.
  [[nodiscard]] virtual BenchComponentStatus prepare() noexcept = 0;
  [[nodiscard]] virtual BenchComponentStatus activate() noexcept = 0;
  [[nodiscard]] virtual BenchComponentStatus service() noexcept = 0;
  [[nodiscard]] virtual BenchComponentStatus safe_stop() noexcept = 0;
  [[nodiscard]] virtual BenchComponentStatus stop() noexcept = 0;

 protected:
  ~IDutSessionEndpoint() = default;
};

struct BenchEnvironmentExecutionContract {
  ecu::core::v2::time::MonotonicDuration max_start_duration{0};
  ecu::core::v2::time::MonotonicDuration max_service_duration{0};
  ecu::core::v2::time::MonotonicDuration max_stop_duration{0};
};

class IEnvironmentSession {
 public:
  [[nodiscard]] virtual BenchEnvironmentExecutionContract
  execution_contract() const noexcept = 0;

  [[nodiscard]] virtual BenchComponentStatus start() noexcept = 0;
  [[nodiscard]] virtual BenchComponentStatus service() noexcept = 0;
  [[nodiscard]] virtual BenchComponentStatus stop() noexcept = 0;

 protected:
  ~IEnvironmentSession() = default;
};

struct BenchElectricalState {
  bool power{false};
  bool ignition{false};
  bool wake{false};
};

[[nodiscard]] constexpr bool operator==(
    const BenchElectricalState lhs,
    const BenchElectricalState rhs) noexcept {
  return lhs.power == rhs.power &&
         lhs.ignition == rhs.ignition &&
         lhs.wake == rhs.wake;
}

[[nodiscard]] constexpr bool operator!=(
    const BenchElectricalState lhs,
    const BenchElectricalState rhs) noexcept {
  return !(lhs == rhs);
}

[[nodiscard]] constexpr bool is_valid_electrical_state(
    const BenchElectricalState state) noexcept {
  return state.power || (!state.ignition && !state.wake);
}

enum class BenchElectricalCapability : std::uint8_t {
  power = 0U,
  ignition = 1U,
  wake_level = 2U,
  wake_pulse = 3U,
  state_feedback = 4U,
  voltage_feedback = 5U,
  current_feedback = 6U,
};

using BenchElectricalCapabilityMask = std::uint32_t;

[[nodiscard]] constexpr BenchElectricalCapabilityMask
bench_electrical_capability_mask(
    const BenchElectricalCapability capability) noexcept {
  const auto bit = static_cast<std::uint8_t>(capability);
  return bit < 32U
             ? static_cast<BenchElectricalCapabilityMask>(
                   static_cast<BenchElectricalCapabilityMask>(1U) << bit)
             : 0U;
}

[[nodiscard]] constexpr bool has_bench_electrical_capability(
    const BenchElectricalCapabilityMask mask,
    const BenchElectricalCapability capability) noexcept {
  const auto required = bench_electrical_capability_mask(capability);
  return required != 0U && (mask & required) == required;
}

struct BenchElectricalFeedback {
  bool state_valid{false};
  BenchElectricalState state{};
  bool voltage_valid{false};
  std::int32_t millivolts{0};
  bool current_valid{false};
  std::int32_t milliamperes{0};
};

struct BenchElectricalExecutionContract {
  ecu::core::v2::time::MonotonicDuration max_apply_duration{0};
  ecu::core::v2::time::MonotonicDuration
      max_wake_pulse_command_duration{0};
  ecu::core::v2::time::MonotonicDuration max_feedback_duration{0};
  ecu::core::v2::time::MonotonicDuration max_safe_off_duration{0};
};

class IBenchElectricalControl {
 public:
  [[nodiscard]] virtual BenchElectricalCapabilityMask
  capabilities() const noexcept = 0;

  [[nodiscard]] virtual BenchElectricalExecutionContract
  execution_contract() const noexcept = 0;

  [[nodiscard]] virtual BenchComponentStatus apply(
      BenchElectricalState state) noexcept = 0;

  // This command must arm/schedule the pulse in the platform adapter and return
  // within its declared execution bound. It must not sleep for the pulse width.
  [[nodiscard]] virtual BenchComponentStatus wake_pulse(
      ecu::core::v2::time::MonotonicDuration pulse_width) noexcept = 0;

  [[nodiscard]] virtual BenchComponentStatus read_feedback(
      BenchElectricalFeedback& feedback) noexcept = 0;

  [[nodiscard]] virtual BenchComponentStatus safe_off() noexcept = 0;

 protected:
  ~IBenchElectricalControl() = default;
};

enum class EnvironmentMode : std::uint8_t {
  none,
  minimal_profile_environment,
};

struct BenchSessionConfig {
  static constexpr std::size_t kMaxAdditionalResources = 8U;

  ecu::core::v2::runtime::ResourceOwnerId session_owner{0U};
  ecu::core::v2::runtime::DutHandle dut{};
  std::array<
      ecu::core::v2::runtime::ResourceKey,
      kMaxAdditionalResources> additional_resources{};
  std::uint8_t additional_resource_count{0U};

  bool use_electrical_control{false};
  BenchElectricalState run_electrical_state{};
  bool use_wake_pulse{false};
  ecu::core::v2::time::MonotonicDuration wake_pulse_width{0};
  bool verify_electrical_state{false};

  EnvironmentMode environment_mode{EnvironmentMode::none};
};

enum class BenchSessionState : std::uint8_t {
  unconfigured,
  ready,
  starting,
  running,
  stopping,
  recovering,
  faulted,
};

enum class BenchSessionStatus : std::uint8_t {
  ok,
  no_action,
  cancelled,
  busy,
  invalid_argument,
  invalid_state,
  resource_unavailable,
  electrical_fault,
  environment_fault,
  dut_fault,
  safe_shutdown_failed,
  execution_contract_invalid,
  cancellation_unavailable,
};

enum class BenchFaultSource : std::uint8_t {
  none,
  resources,
  electrical,
  environment,
  dut,
  safe_shutdown,
};

struct BenchSessionCounters {
  std::uint32_t starts{0U};
  std::uint32_t successful_starts{0U};
  std::uint32_t services{0U};
  std::uint32_t stops{0U};
  std::uint32_t cancellations{0U};
  std::uint32_t recoveries{0U};
  std::uint32_t resource_contentions{0U};
  std::uint32_t faults{0U};
  std::uint32_t safe_shutdowns{0U};
  std::uint32_t safe_shutdown_failures{0U};
};

struct BenchSessionExecutionBudget {
  bool valid{false};
  ecu::core::v2::time::MonotonicDuration max_start_duration{0};
  ecu::core::v2::time::MonotonicDuration max_service_duration{0};
  ecu::core::v2::time::MonotonicDuration max_stop_duration{0};
};

class BenchSession final {
 public:
  static constexpr std::size_t kMaxLeases =
      BenchSessionConfig::kMaxAdditionalResources + 1U;

  BenchSession(
      ecu::core::v2::runtime::ResourceManager& resources,
      const ecu::core::v2::runtime::DutRegistry& duts,
      IDutSessionEndpoint& endpoint,
      IBenchElectricalControl* electrical,
      IEnvironmentSession* environment) noexcept;

  BenchSession(const BenchSession&) = delete;
  BenchSession& operator=(const BenchSession&) = delete;
  BenchSession(BenchSession&&) = delete;
  BenchSession& operator=(BenchSession&&) = delete;

  [[nodiscard]] bool configure(
      const BenchSessionConfig& config) noexcept;

  [[nodiscard]] BenchSessionStatus start() noexcept;
  [[nodiscard]] BenchSessionStatus service() noexcept;
  [[nodiscard]] BenchSessionStatus stop() noexcept;
  [[nodiscard]] BenchSessionStatus recover() noexcept;

  [[nodiscard]] ecu::core::v2::runtime::CancellationRequestStatus
  request_cancel(
      ecu::core::v2::runtime::CancellationToken token) noexcept;

  [[nodiscard]] ecu::core::v2::runtime::CancellationToken
  cancellation_token() const noexcept;

  [[nodiscard]] BenchSessionState state() const noexcept;
  [[nodiscard]] BenchSessionStatus status() const noexcept;
  [[nodiscard]] BenchFaultSource fault_source() const noexcept;
  [[nodiscard]] BenchSessionCounters counters() const noexcept;
  [[nodiscard]] BenchSessionExecutionBudget execution_budget()
      const noexcept;
  [[nodiscard]] bool cleanup_required() const noexcept;

  [[nodiscard]] const ecu::core::v2::domain::DutDescriptor*
  dut_descriptor() const noexcept;

 private:
  [[nodiscard]] bool valid_config(
      const BenchSessionConfig& config,
      const ecu::core::v2::domain::DutDescriptor& dut) const noexcept;
  [[nodiscard]] bool resource_list_valid(
      const BenchSessionConfig& config) const noexcept;
  [[nodiscard]] bool electrical_configuration_supported(
      const BenchSessionConfig& config) const noexcept;
  [[nodiscard]] bool component_contracts_valid() const noexcept;
  [[nodiscard]] bool execution_budget_valid(
      BenchSessionExecutionBudget& budget) const noexcept;
  [[nodiscard]] bool add_duration(
      ecu::core::v2::time::MonotonicDuration left,
      ecu::core::v2::time::MonotonicDuration right,
      ecu::core::v2::time::MonotonicDuration& result) const noexcept;

  [[nodiscard]] BenchSessionStatus acquire_resources() noexcept;
  [[nodiscard]] bool release_resources() noexcept;

  [[nodiscard]] BenchSessionStatus fail_start(
      BenchSessionStatus status,
      BenchFaultSource source) noexcept;
  [[nodiscard]] BenchSessionStatus trip_runtime_fault(
      BenchSessionStatus status,
      BenchFaultSource source) noexcept;
  [[nodiscard]] BenchSessionStatus cancel_running() noexcept;
  [[nodiscard]] bool safe_shutdown() noexcept;
  void complete_operation() noexcept;
  [[nodiscard]] bool verify_electrical_feedback(
      const BenchElectricalFeedback& feedback) const noexcept;

  ecu::core::v2::runtime::ResourceManager& resources_;
  const ecu::core::v2::runtime::DutRegistry& duts_;
  IDutSessionEndpoint& endpoint_;
  IBenchElectricalControl* electrical_{nullptr};
  IEnvironmentSession* environment_{nullptr};

  BenchSessionConfig config_{};
  const ecu::core::v2::domain::DutDescriptor* dut_{nullptr};
  std::array<
      ecu::core::v2::runtime::ResourceLease,
      kMaxLeases> leases_{};
  std::uint8_t lease_count_{0U};

  ecu::core::v2::runtime::CancellationSource cancellation_{};
  ecu::core::v2::runtime::CancellationToken operation_token_{};

  BenchSessionExecutionBudget budget_{};
  BenchSessionCounters counters_{};
  BenchSessionState state_{BenchSessionState::unconfigured};
  BenchSessionStatus status_{BenchSessionStatus::invalid_state};
  BenchFaultSource fault_source_{BenchFaultSource::none};

  bool configured_{false};
  bool endpoint_prepared_{false};
  bool endpoint_activated_{false};
  bool electrical_cleanup_needed_{false};
  bool environment_started_{false};
  bool cleanup_required_{false};
  bool busy_{false};
};

}  // namespace ecu::bench
