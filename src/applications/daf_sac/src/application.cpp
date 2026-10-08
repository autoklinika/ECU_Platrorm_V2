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
  // Two independent immutable SAC DUT profiles. NEVER assume that the
  // selected profile and the CAN link bitrate belong to the same variant.
  // Core V2 and Bench Runtime remain transport/DUT neutral.
  const bool known_sac_variant =
      (plan.profile_id == daf::profile_id(daf::CanBitrateProfile::k250k) &&
       plan.can_links[0U].channel_config.nominal_bitrate == 250000U) ||
      (plan.profile_id == daf::profile_id(daf::CanBitrateProfile::k500k) &&
       plan.can_links[0U].channel_config.nominal_bitrate == 500000U);
  return plan.schema_version ==
             ecu::dut_profile::DutProfileDefinition::kSchemaVersion &&
         known_sac_variant &&
         plan.profile_revision == daf::kProfileRevision &&
         plan.can_link_count == 1U && plan.rx_expectation_count == 2U &&
         plan.rx_expectations[0U].identifier == daf::kResponseCanId &&
         plan.rx_expectations[0U].mask == 0x1FFFFFFFU &&
         plan.rx_expectations[1U].identifier == daf::kPressureCanId &&
         plan.rx_expectations[1U].mask == daf::kPressureCanMask &&
         plan.rx_expectations[1U].match_extended &&
         !plan.rx_expectations[1U].match_standard &&
         plan.can_links[0U].link_id == daf::kPrimaryCanLink &&
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
      program_(&program) {
  configure_execution(driver_execution);
}

BenchEndpoint::BenchEndpoint(
    const ecu::dut_profile::ResolvedDutSessionPlan& plan,
    ecu::core::v2::transport::CanBusRuntime& bus,
    ecu::dut_profile::DutProfileSessionEndpoint& profile_endpoint,
    ecu::dut_profiles::daf_sac::ServiceProgram& program,
    const ecu::core::v2::transport::CanDriverExecutionContract driver_execution)
    noexcept
    : plan_(plan),
      bus_(bus),
      profile_endpoint_(profile_endpoint),
      services_(&program) {
  configure_execution(driver_execution);
}

void BenchEndpoint::configure_execution(
    const ecu::core::v2::transport::CanDriverExecutionContract driver_execution)
    noexcept {
  const auto profile = profile_endpoint_.execution_contract();
  const auto callback = bus_.max_single_frame_dispatch_duration();
  if (!valid_plan(plan_) || !profile_endpoint_.valid() ||
      bus_.state() != CanBusState::ready ||
      bus_.subscription_count() != (services_ == nullptr ? 1U : 2U) ||
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
  if (services_ != nullptr) {
    last_uds_status_ = services_->last_uds_status();
    last_transport_failure_ = services_->last_transport_failure();
    last_nrc_ = services_->last_nrc();
  } else if (program_ != nullptr) {
    last_uds_status_ = program_->last_uds_status();
    last_transport_failure_ = program_->last_transport_failure();
    last_nrc_ = program_->last_nrc();
  }
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
      program_(&program),
      clock_(clock) {}

Application::Application(
    const ecu::dut_profile::ResolvedDutSessionPlan& plan,
    BenchEndpoint& endpoint,
    ecu::bench::BenchSession& session,
    ecu::bench::BenchSessionHostRuntime& host,
    ecu::dut_profiles::daf_sac::ServiceProgram& services,
    ecu::dut_profiles::daf_sac::PressureMonitor& pressure,
    const ecu::core::v2::time::IMonotonicClock& clock) noexcept
    : plan_(plan),
      endpoint_(endpoint),
      session_(session),
      host_(host),
      services_(&services),
      pressure_(&pressure),
      clock_(clock) {}

bool Application::configure(
    const ecu::bench::BenchHostServiceConfig host_config) noexcept {
  if (state_ != AppState::unconfigured || !valid_plan(plan_) ||
      !endpoint_.valid() ||
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

bool Application::operation_active() const noexcept {
  return state_ == AppState::identifying ||
         state_ == AppState::reading_parameters ||
         state_ == AppState::reading_dtcs ||
         state_ == AppState::clearing_dtcs;
}

void Application::invalidate_clear_challenge() noexcept {
  armed_clear_sequence_ = 0U;
}

bool Application::fresh_dtc_inventory() const noexcept {
  if (!last_dtc_read_valid_) {
    return false;
  }
  const auto now = clock_.read();
  return ecu::core::v2::time::is_valid_clock_reading(
             now, clock_.properties().domain) &&
         now.value >= last_dtc_read_at_ &&
         now.value - last_dtc_read_at_ <= std::chrono::seconds{180};
}

AppStatus Application::begin_operation(const AppState state) noexcept {
  invalidate_clear_challenge();
  const auto started = host_.start();
  if (started.status != HostStatus::ok) {
    return fail(started);
  }
  const auto now = clock_.read();
  constexpr auto kMaxOperation = std::chrono::seconds{10};
  const auto max_time = (std::numeric_limits<Duration::rep>::max)();
  if (!ecu::core::v2::time::is_valid_clock_reading(
          now, clock_.properties().domain) ||
      now.value.count() >
          max_time - std::chrono::duration_cast<Duration>(
                         kMaxOperation).count()) {
    const auto stopped = host_.stop();
    state_ = AppState::faulted;
    status_ =
        (stopped.status == HostStatus::ok ||
         stopped.status == HostStatus::no_action)
            ? AppStatus::runtime_fault
            : AppStatus::safe_shutdown_failed;
    return status_;
  }
  deadline_ = now.value + kMaxOperation;
  pressure_wait_deadline_ = now.value + std::chrono::milliseconds{800};
  state_ = state;
  status_ = AppStatus::ok;
  return status_;
}

AppStatus Application::identify() noexcept {
  if (state_ == AppState::unconfigured ||
      state_ == AppState::faulted || operation_active()) {
    return AppStatus::invalid_state;
  }
  if (services_ != nullptr &&
      !services_->select(ecu::dut_profiles::daf_sac::SacService::identify)) {
    return AppStatus::invalid_state;
  }
  record_ = {};
  record_valid_ = false;
  return begin_operation(AppState::identifying);
}

AppStatus Application::read_parameters() noexcept {
  if (services_ == nullptr || pressure_ == nullptr) {
    return AppStatus::unsupported;
  }
  if (state_ == AppState::unconfigured ||
      state_ == AppState::faulted || operation_active()) {
    return AppStatus::invalid_state;
  }
  if (!services_->select(ecu::dut_profiles::daf_sac::SacService::read_voltage)) {
    return AppStatus::invalid_state;
  }
  pressure_->reset();
  voltage_ = {};
  pressure_sample_ = {};
  return begin_operation(AppState::reading_parameters);
}

AppStatus Application::read_dtcs(const std::uint8_t mask) noexcept {
  if (services_ == nullptr) {
    return AppStatus::unsupported;
  }
  if (state_ == AppState::unconfigured ||
      state_ == AppState::faulted || operation_active()) {
    return AppStatus::invalid_state;
  }
  if (!services_->select(
          ecu::dut_profiles::daf_sac::SacService::read_dtcs, mask)) {
    return AppStatus::invalid_state;
  }
  dtcs_ = {};
  last_dtc_read_valid_ = false;
  clear_acknowledged_ = false;
  return begin_operation(AppState::reading_dtcs);
}

ClearDtcChallenge Application::prepare_clear_dtcs() noexcept {
  // Erasing ALL DTC groups cannot be authorized from a filtered inventory.
  // DTC clear has been exercised only on the 250k DUT. Do not expose
  // destructive operations on the new, unverified 500k variant.
  if (services_ == nullptr ||
      plan_.profile_id !=
          ecu::dut_profiles::daf_sac::profile_id(
              ecu::dut_profiles::daf_sac::CanBitrateProfile::k250k) ||
      state_ != AppState::dtcs_ready ||
      !dtcs_.valid || dtcs_.requested_mask != 0xFFU ||
      !fresh_dtc_inventory() ||
      session_.state() != ecu::bench::BenchSessionState::ready ||
      next_clear_sequence_ ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    return {};
  }
  ++next_clear_sequence_;
  armed_clear_sequence_ = next_clear_sequence_;
  return {armed_clear_sequence_, plan_.profile_id, dtcs_.count};
}

AppStatus Application::clear_dtcs(
    const ClearDtcChallenge& confirmation,
    const bool explicitly_confirmed) noexcept {
  if (services_ == nullptr) {
    return AppStatus::unsupported;
  }
  if (plan_.profile_id !=
          ecu::dut_profiles::daf_sac::profile_id(
              ecu::dut_profiles::daf_sac::CanBitrateProfile::k250k) ||
      state_ != AppState::dtcs_ready || !dtcs_.valid ||
      dtcs_.requested_mask != 0xFFU || !fresh_dtc_inventory() ||
      armed_clear_sequence_ == 0U || !explicitly_confirmed ||
      confirmation.sequence != armed_clear_sequence_ ||
      confirmation.profile_id != plan_.profile_id ||
      confirmation.inspected_dtc_count != dtcs_.count) {
    return AppStatus::confirmation_required;
  }
  // Consume the token before starting the session. Retry requires a new
  // successful DTC read and a new explicit confirmation.
  invalidate_clear_challenge();
  if (!services_->select(ecu::dut_profiles::daf_sac::SacService::clear_dtcs)) {
    return AppStatus::invalid_state;
  }
  dtcs_ = {}; // previous read is stale as soon as clear is attempted
  last_dtc_read_valid_ = false;
  clear_acknowledged_ = false;
  return begin_operation(AppState::clearing_dtcs);
}

AppStatus Application::service() noexcept {
  if (!operation_active()) {
    return AppStatus::invalid_state;
  }

  const auto now = clock_.read();
  if (!ecu::core::v2::time::is_valid_clock_reading(
          now, clock_.properties().domain) ||
      now.value >= deadline_) {
    const bool erase_may_have_been_sent =
        state_ == AppState::clearing_dtcs && services_ != nullptr &&
        services_->clear_request_submitted();
    const auto stopped = host_.stop();
    record_ = {};
    record_valid_ = false;
    voltage_ = {};
    pressure_sample_ = {};
    dtcs_ = {};
    state_ = AppState::faulted;
    status_ =
        (stopped.status == HostStatus::ok ||
         stopped.status == HostStatus::no_action)
            ? (erase_may_have_been_sent
                   ? AppStatus::clear_outcome_unknown
                   : AppStatus::timeout)
            : AppStatus::safe_shutdown_failed;
    return status_;
  }

  const auto serviced = host_.service();
  if (serviced.status != HostStatus::ok &&
      serviced.status != HostStatus::no_action) {
    return fail(serviced);
  }

  const auto complete = services_ == nullptr
      ? program_->status() ==
          ecu::dut_profiles::daf_sac::IdentificationProgramStatus::complete
      : services_->status() ==
          ecu::dut_profiles::daf_sac::IdentificationProgramStatus::complete;

  if (!complete) {
    return AppStatus::no_action;
  }
  // The FE96 response can arrive before the next periodic PGN 0xFEAE.
  // Continue bounded receive-only polling for at most 800ms to give
  // the SAC pressure broadcast a chance to arrive.
  if (state_ == AppState::reading_parameters && pressure_ != nullptr &&
      !pressure_->sample().received && now.value < pressure_wait_deadline_) {
    return AppStatus::no_action;
  }

  const auto prior_state = state_;
  bool payload_valid = false;
  if (prior_state == AppState::identifying) {
    const auto& result = services_ != nullptr
        ? services_->identification() : program_->result();
    const bool vin_valid =
        result.vin.length == 17U && !result.vin_unprogrammed_ff17;
    const bool vin_marker_500k =
        plan_.profile_id ==
            ecu::dut_profiles::daf_sac::profile_id(
                ecu::dut_profiles::daf_sac::CanBitrateProfile::k500k) &&
        result.vin_unprogrammed_ff17 && result.vin.length == 0U;
    payload_valid = (vin_valid || vin_marker_500k) &&
                    result.software.length > 0U &&
                    result.hardware.length > 0U;
    if (payload_valid) {
      record_ = result;
      record_valid_ = true;
    }
  } else if (prior_state == AppState::reading_parameters) {
    voltage_ = services_->voltage();
    pressure_sample_ = pressure_->sample();
    // A valid FE96 voltage response can succeed even if the ECU has
    // broadcast pressure marked as unavailable / not supported on the bench.
    payload_valid = voltage_.valid;
  } else if (prior_state == AppState::reading_dtcs) {
    dtcs_ = services_->dtcs();
    payload_valid = dtcs_.valid;
  } else if (prior_state == AppState::clearing_dtcs) {
    clear_acknowledged_ = services_->clear_acknowledged();
    payload_valid = clear_acknowledged_;
  }

  const auto stopped = host_.stop();
  if (stopped.status != HostStatus::ok &&
      stopped.status != HostStatus::no_action) {
    return fail(stopped);
  }
  if (!payload_valid) {
    record_ = {};
    record_valid_ = false;
    voltage_ = {};
    pressure_sample_ = {};
    dtcs_ = {};
    state_ = AppState::faulted;
    status_ = AppStatus::runtime_fault;
    return status_;
  }

  switch (prior_state) {
    case AppState::identifying: state_ = AppState::identified; break;
    case AppState::reading_parameters: state_ = AppState::parameters_ready; break;
    case AppState::reading_dtcs:
      last_dtc_read_at_ = now.value;
      last_dtc_read_valid_ = true;
      state_ = AppState::dtcs_ready;
      break;
    case AppState::clearing_dtcs:
      state_ = AppState::dtcs_clear_acknowledged; break;
    default: state_ = AppState::faulted; break;
  }
  status_ = state_ == AppState::faulted
                ? AppStatus::runtime_fault : AppStatus::ok;
  return status_;
}

AppStatus Application::stop() noexcept {
  if (!operation_active()) {
    return (state_ != AppState::unconfigured &&
            state_ != AppState::faulted)
        ? AppStatus::no_action : AppStatus::invalid_state;
  }
  const auto stopped = host_.stop();
  if (stopped.status != HostStatus::ok &&
      stopped.status != HostStatus::no_action) {
    return fail(stopped);
  }
  record_ = {};
  record_valid_ = false;
  voltage_ = {};
  pressure_sample_ = {};
  dtcs_ = {};
  clear_acknowledged_ = false;
  invalidate_clear_challenge();
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
  voltage_ = {};
  pressure_sample_ = {};
  dtcs_ = {};
  clear_acknowledged_ = false;
  invalidate_clear_challenge();
  state_ = AppState::ready;
  status_ = AppStatus::ok;
  return status_;
}

AppSnapshot Application::snapshot() const noexcept {
  AppSnapshot out{};
  out.state = state_;
  out.status = status_;
  out.profile_id = plan_.profile_id;
  out.bitrate = plan_.can_link_count != 0U
      ? plan_.can_links[0U].channel_config.nominal_bitrate : 0U;
  out.bench = session_.snapshot();
  out.can_status = endpoint_.last_can_status();
  out.uds_status = endpoint_.last_uds_status();
  out.transport_failure = endpoint_.last_transport_failure();
  out.nrc = endpoint_.last_nrc();
  out.identification_available = record_valid_;
  out.voltage_available = voltage_.valid;
  out.pressure_received = pressure_sample_.received;
  out.pressure1_valid = pressure_sample_.pressure1_valid;
  out.pressure2_valid = pressure_sample_.pressure2_valid;
  out.dtcs_available = dtcs_.valid;
  out.dtc_count = dtcs_.count;
  out.clear_acknowledged = clear_acknowledged_;
  // A partial identity (e.g. the SAC 500k F190 FF sentinel) must not
  // fabricate a four-character VIN suffix in future GUI/API telemetry.
  if (record_valid_ && !record_.vin_unprogrammed_ff17 &&
      record_.vin.length == 17U) {
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

ecu::dut_profiles::daf_sac::SacVoltage
Application::voltage() const noexcept {
  return voltage_;
}

ecu::dut_profiles::daf_sac::SacPressure
Application::pressure() const noexcept {
  return pressure_sample_;
}

const ecu::dut_profiles::daf_sac::SacDtcList&
Application::dtcs() const noexcept {
  return dtcs_;
}

AppStatus Application::fail(
    const ecu::bench::BenchHostServiceResult& result) noexcept {
  const bool erase_may_have_been_sent =
      state_ == AppState::clearing_dtcs && services_ != nullptr &&
      services_->clear_request_submitted() &&
      services_->last_nrc() == 0U;
  record_ = {};
  record_valid_ = false;
  voltage_ = {};
  pressure_sample_ = {};
  dtcs_ = {};
  clear_acknowledged_ = false;
  invalidate_clear_challenge();
  state_ = AppState::faulted;
  if (result.status == HostStatus::deadline_missed) {
    status_ = erase_may_have_been_sent
                  ? AppStatus::clear_outcome_unknown
                  : AppStatus::timeout;
  } else if (result.status == HostStatus::safe_shutdown_failed ||
             result.session_status ==
                 ecu::bench::BenchSessionStatus::safe_shutdown_failed) {
    status_ = AppStatus::safe_shutdown_failed;
  } else if (result.session_status ==
                 ecu::bench::BenchSessionStatus::resource_unavailable) {
    status_ = AppStatus::resource_unavailable;
  } else {
    status_ = erase_may_have_been_sent
                  ? AppStatus::clear_outcome_unknown
                  : AppStatus::runtime_fault;
  }
  return status_;
}

}  // namespace ecu::applications::daf_sac
