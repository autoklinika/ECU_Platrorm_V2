#include "ecu/applications/daf_sac/application.hpp"

#include <chrono>
#include <limits>

namespace ecu::applications::daf_sac {
namespace {

using Duration = ecu::core::v2::time::MonotonicDuration;
using Count = Duration::rep;
using CanStatus = ecu::core::v2::transport::CanStatus;
using CanBusState = ecu::core::v2::transport::CanBusState;
using ComponentStatus = ecu::bench::BenchComponentStatus;
using HostStatus = ecu::bench::BenchHostServiceStatus;

[[nodiscard]] bool add(Duration& total, const Duration increment) noexcept {
  if (total.count() < 0 || increment.count() < 0 ||
      total.count() > (std::numeric_limits<Count>::max)() -
                          increment.count()) {
    return false;
  }
  total += increment;
  return true;
}

[[nodiscard]] bool add_repeated(
    Duration& total,
    const Duration duration,
    const std::size_t count) noexcept {
  if (duration.count() < 0 ||
      (duration.count() > 0 &&
       count > static_cast<std::size_t>(
                   (std::numeric_limits<Count>::max)() / duration.count()))) {
    return false;
  }
  return add(total, duration * static_cast<Count>(count));
}

[[nodiscard]] bool nonzero_contract(
    const ecu::bench::BenchComponentExecutionContract& contract) noexcept {
  return contract.max_prepare_duration.count() > 0 &&
         contract.max_activate_duration.count() > 0 &&
         contract.max_service_duration.count() > 0 &&
         contract.max_safe_stop_duration.count() > 0 &&
         contract.max_stop_duration.count() > 0;
}

[[nodiscard]] bool valid_plan(
    const ecu::dut_profile::ResolvedDutSessionPlan& plan) noexcept {
  namespace daf = ecu::dut_profiles::daf_sac;
  namespace transport = ecu::core::v2::transport;
  return plan.schema_version ==
             ecu::dut_profile::DutProfileDefinition::kSchemaVersion &&
         plan.profile_id == daf::profile_id(daf::CanBitrateProfile::k250k) &&
         plan.profile_revision == daf::kProfileRevision &&
         plan.can_link_count == 1U && plan.rx_expectation_count == 1U &&
         plan.can_links[0U].link_id == daf::kPrimaryCanLink &&
         plan.can_links[0U].channel_config.nominal_bitrate == 250000U &&
         !plan.can_links[0U].channel_config.fd_enabled &&
         plan.can_links[0U].channel_config.mode ==
             transport::CanMode::normal &&
         plan.bench.session_owner != 0U &&
         plan.bench.additional_resource_count == 1U &&
         plan.bench.additional_resources[0U] ==
             plan.can_links[0U].resource;
}

}  // namespace

BenchEndpoint::BenchEndpoint(
    const ecu::dut_profile::ResolvedDutSessionPlan& plan,
    ecu::core::v2::transport::CanBusRuntime& bus,
    ecu::dut_profile::DutProfileSessionEndpoint& profile_endpoint,
    ecu::dut_profiles::daf_sac::IdentificationProgram& program,
    const ecu::core::v2::transport::CanDriverExecutionContract driver_execution)
    noexcept
    : plan_(plan),
      bus_(bus),
      profile_endpoint_(profile_endpoint),
      program_(program) {
  const auto profile = profile_endpoint_.execution_contract();
  const auto callback = bus_.max_single_frame_dispatch_duration();
  if (!valid_plan(plan_) || !profile_endpoint_.valid() ||
      bus_.state() != CanBusState::ready ||
      bus_.subscription_count() != 1U ||
      !ecu::core::v2::transport::is_valid_can_driver_execution_contract(
          driver_execution) ||
      !nonzero_contract(profile) || callback.count() <= 0) {
    return;
  }

  Duration receive_and_callback{driver_execution.max_try_receive_duration};
  if (!add(receive_and_callback, callback)) {
    return;
  }

  execution_ = profile;
  valid_ =
      add(execution_.max_prepare_duration,
          driver_execution.max_lease_acquire_duration) &&
      add(execution_.max_prepare_duration,
          driver_execution.max_open_duration) &&
      add(execution_.max_prepare_duration,
          driver_execution.max_close_duration) &&
      add(execution_.max_activate_duration, Duration{1}) &&
      add_repeated(
          execution_.max_service_duration,
          receive_and_callback,
          kMaxPollFrames) &&
      add(execution_.max_service_duration,
          driver_execution.max_close_duration) &&
      add(execution_.max_safe_stop_duration, Duration{1}) &&
      add(execution_.max_stop_duration,
          driver_execution.max_close_duration) &&
      add(execution_.max_stop_duration,
          driver_execution.max_lease_release_duration);
  if (!valid_) {
    execution_ = {};
  }
}

bool BenchEndpoint::valid() const noexcept {
  return valid_;
}

ecu::bench::BenchComponentExecutionContract
BenchEndpoint::execution_contract() const noexcept {
  return valid_ ? execution_ : ecu::bench::BenchComponentExecutionContract{};
}

ComponentStatus BenchEndpoint::prepare() noexcept {
  if (!valid_ || prepared_ || active_) {
    return ComponentStatus::fault;
  }
  last_can_status_ = bus_.start(plan_.can_links[0U].channel_config);
  if (last_can_status_ != CanStatus::ok) {
    (void)close_bus();
    return ComponentStatus::fault;
  }
  bus_started_ = true;
  // Mark prepared before calling the profile, so Bench can execute its
  // safe-stop/stop cleanup if the profile itself fails during prepare().
  prepared_ = true;
  const auto result = profile_endpoint_.prepare();
  if (result != ComponentStatus::ok &&
      result != ComponentStatus::no_action) {
    capture_diagnostic_status();
    return ComponentStatus::fault;
  }
  return ComponentStatus::ok;
}

ComponentStatus BenchEndpoint::activate() noexcept {
  if (!valid_ || !prepared_ || active_) {
    return ComponentStatus::fault;
  }
  const auto result = profile_endpoint_.activate();
  if (result != ComponentStatus::ok &&
      result != ComponentStatus::no_action) {
    capture_diagnostic_status();
    return ComponentStatus::fault;
  }
  active_ = true;
  return ComponentStatus::ok;
}

ComponentStatus BenchEndpoint::service() noexcept {
  if (!valid_ || !prepared_ || !active_ || !bus_started_) {
    return ComponentStatus::fault;
  }

  const auto received = bus_.poll(kMaxPollFrames);
  last_can_status_ = received.status;
  if (received.status != CanStatus::ok) {
    return ComponentStatus::fault;
  }

  const auto result = profile_endpoint_.service();
  capture_diagnostic_status();
  return result;
}

ComponentStatus BenchEndpoint::safe_stop() noexcept {
  if (!valid_) {
    return ComponentStatus::fault;
  }
  if (!prepared_) {
    return ComponentStatus::no_action;
  }

  active_ = false;
  capture_diagnostic_status();
  return profile_endpoint_.safe_stop();
}

ComponentStatus BenchEndpoint::stop() noexcept {
  if (!valid_) {
    return ComponentStatus::fault;
  }

  capture_diagnostic_status();
  ComponentStatus result = ComponentStatus::ok;
  if (prepared_) {
    result = profile_endpoint_.stop();
  }
  prepared_ = false;
  active_ = false;
  const auto bus_closed = close_bus();
  if (!bus_closed || (result != ComponentStatus::ok &&
                      result != ComponentStatus::no_action)) {
    return ComponentStatus::fault;
  }
  return ComponentStatus::ok;
}

CanStatus BenchEndpoint::last_can_status() const noexcept {
  return last_can_status_;
}

ecu::core::v2::protocol::uds::UdsStatus
BenchEndpoint::last_uds_status() const noexcept {
  return last_uds_status_;
}

ecu::core::v2::protocol::uds::UdsTransportFailure
BenchEndpoint::last_transport_failure() const noexcept {
  return last_transport_failure_;
}

std::uint8_t BenchEndpoint::last_nrc() const noexcept {
  return last_nrc_;
}

void BenchEndpoint::capture_diagnostic_status() noexcept {
  last_uds_status_ = program_.last_uds_status();
  last_transport_failure_ = program_.last_transport_failure();
  last_nrc_ = program_.last_nrc();
}

bool BenchEndpoint::close_bus() noexcept {
  bus_started_ = false;
  if (bus_.state() == CanBusState::faulted) {
    const auto recovered = bus_.recover();
    if (recovered != CanStatus::ok) {
      last_can_status_ = recovered;
    }
    return recovered == CanStatus::ok;
  }
  if (bus_.state() == CanBusState::running) {
    bus_.stop();
  }
  return bus_.state() == CanBusState::stopped ||
         bus_.state() == CanBusState::ready;
}

Application::Application(
    const ecu::dut_profile::ResolvedDutSessionPlan& plan,
    BenchEndpoint& endpoint,
    ecu::bench::BenchSession& session,
    ecu::bench::BenchSessionHostRuntime& host,
    ecu::dut_profiles::daf_sac::IdentificationProgram& program,
    const ecu::core::v2::time::IMonotonicClock& clock) noexcept
    : plan_(plan),
      endpoint_(endpoint),
      session_(session),
      host_(host),
      program_(program),
      clock_(clock) {}

bool Application::configure(
    const ecu::bench::BenchHostServiceConfig host_config) noexcept {
  if (state_ != AppState::unconfigured ||
      !valid_plan(plan_) || !endpoint_.valid() ||
      !ecu::core::v2::time::is_valid_clock_properties(clock_.properties()) ||
      clock_.properties().domain !=
          plan_.can_links[0U].channel_config.timestamp_domain ||
      !session_.configure(plan_.bench) ||
      !host_.configure(host_config)) {
    status_ = AppStatus::configuration_failed;
    return false;
  }
  state_ = AppState::ready;
  status_ = AppStatus::ok;
  return true;
}

AppStatus Application::identify() noexcept {
  if (state_ != AppState::ready &&
      state_ != AppState::identified) {
    return AppStatus::invalid_state;
  }
  record_ = {};
  record_valid_ = false;
  const auto started = host_.start();
  if (started.status != HostStatus::ok) {
    return fail(started);
  }
  const auto now = clock_.read();
  constexpr auto kMaxIdentification = std::chrono::seconds{10};
  const auto max_time =
      (std::numeric_limits<Duration::rep>::max)();
  if (!ecu::core::v2::time::is_valid_clock_reading(
          now, clock_.properties().domain) ||
      now.value.count() > max_time - kMaxIdentification.count() * 1000000000LL) {
    const auto stopped = host_.stop();
    state_ = AppState::faulted;
    status_ =
        (stopped.status == HostStatus::ok ||
         stopped.status == HostStatus::no_action)
            ? AppStatus::runtime_fault
            : AppStatus::safe_shutdown_failed;
    return status_;
  }
  deadline_ = now.value + kMaxIdentification;
  state_ = AppState::identifying;
  status_ = AppStatus::ok;
  return status_;
}

AppStatus Application::service() noexcept {
  if (state_ != AppState::identifying) {
    return AppStatus::invalid_state;
  }

  const auto now = clock_.read();
  if (!ecu::core::v2::time::is_valid_clock_reading(
          now, clock_.properties().domain) ||
      now.value >= deadline_) {
    const auto stopped = host_.stop();
    record_ = {};
    record_valid_ = false;
    state_ = AppState::faulted;
    status_ =
        (stopped.status == HostStatus::ok ||
         stopped.status == HostStatus::no_action)
            ? AppStatus::timeout
            : AppStatus::safe_shutdown_failed;
    return status_;
  }

  const auto serviced = host_.service();
  if (serviced.status != HostStatus::ok &&
      serviced.status != HostStatus::no_action) {
    return fail(serviced);
  }

  if (program_.status() !=
      ecu::dut_profiles::daf_sac::IdentificationProgramStatus::complete) {
    return AppStatus::no_action;
  }

  const auto& result = program_.result();
  if (result.vin.length == 17U &&
      result.software.length > 0U && result.hardware.length > 0U) {
    record_ = result;
    record_valid_ = true;
  }

  const auto stopped = host_.stop();
  if (stopped.status != HostStatus::ok &&
      stopped.status != HostStatus::no_action) {
    record_ = {};
    record_valid_ = false;
    return fail(stopped);
  }
  if (!record_valid_) {
    state_ = AppState::faulted;
    status_ = AppStatus::runtime_fault;
    return status_;
  }
  state_ = AppState::identified;
  status_ = AppStatus::ok;
  return status_;
}

AppStatus Application::stop() noexcept {
  if (state_ == AppState::ready ||
      state_ == AppState::identified) {
    return AppStatus::no_action;
  }
  if (state_ != AppState::identifying) {
    return AppStatus::invalid_state;
  }
  const auto stopped = host_.stop();
  if (stopped.status != HostStatus::ok &&
      stopped.status != HostStatus::no_action) {
    return fail(stopped);
  }
  record_ = {};
  record_valid_ = false;
  state_ = AppState::ready;
  status_ = AppStatus::ok;
  return status_;
}

AppStatus Application::recover() noexcept {
  if (state_ != AppState::faulted) {
    return AppStatus::invalid_state;
  }
  if (session_.state() == ecu::bench::BenchSessionState::faulted) {
    const auto recovered = host_.recover();
    if (recovered.status != HostStatus::ok) {
      return fail(recovered);
    }
  }
  if (session_.state() != ecu::bench::BenchSessionState::ready) {
    return AppStatus::invalid_state;
  }
  record_ = {};
  record_valid_ = false;
  state_ = AppState::ready;
  status_ = AppStatus::ok;
  return status_;
}

AppSnapshot Application::snapshot() const noexcept {
  AppSnapshot out{};
  out.state = state_;
  out.status = status_;
  out.profile_id = plan_.profile_id;
  out.bitrate =
      plan_.can_link_count != 0U
          ? plan_.can_links[0U].channel_config.nominal_bitrate
          : 0U;
  out.bench = session_.snapshot();
  out.can_status = endpoint_.last_can_status();
  out.uds_status = endpoint_.last_uds_status();
  out.transport_failure = endpoint_.last_transport_failure();
  out.nrc = endpoint_.last_nrc();
  out.identification_available = record_valid_;
  if (record_valid_) {
    for (std::size_t i = 0U; i < 4U; ++i) {
      out.vin_suffix[i] = record_.vin.data[13U + i];
    }
  }
  return out;
}

const ecu::dut_profiles::daf_sac::IdentificationResult&
Application::identification() const noexcept {
  return record_;
}

AppStatus Application::fail(
    const ecu::bench::BenchHostServiceResult& result) noexcept {
  record_ = {};
  record_valid_ = false;
  state_ = AppState::faulted;
  if (result.status == HostStatus::deadline_missed) {
    status_ = AppStatus::timeout;
  } else if (result.status == HostStatus::safe_shutdown_failed ||
             result.session_status ==
                 ecu::bench::BenchSessionStatus::safe_shutdown_failed) {
    status_ = AppStatus::safe_shutdown_failed;
  } else if (result.session_status ==
                 ecu::bench::BenchSessionStatus::resource_unavailable) {
    status_ = AppStatus::resource_unavailable;
  } else {
    status_ = AppStatus::runtime_fault;
  }
  return status_;
}

}  // namespace ecu::applications::daf_sac
