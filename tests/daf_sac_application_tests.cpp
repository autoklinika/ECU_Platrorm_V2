#include "ecu/applications/daf_sac/application.hpp"
#include "ecu/core_v2/runtime/resource_manager.hpp"
#include "ecu/dut_profile/registry.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
namespace app = ecu::applications::daf_sac;
namespace bench = ecu::bench;
namespace core = ecu::core::v2;
namespace daf = ecu::dut_profiles::daf_sac;
namespace dp = ecu::dut_profile;
namespace transport = ecu::core::v2::transport;
namespace uds = ecu::core::v2::protocol::uds;
namespace runtime = ecu::core::v2::runtime;

constexpr core::time::MonotonicClockDomainId kDomain{77U};
constexpr runtime::ResourceKey kPhysicalCan{
    runtime::ResourceClass::can_channel, 1U};

int require(bool condition, const char* reason) {
  if (!condition) {
    std::cerr << "FAIL: " << reason << '\n';
    return 1;
  }
  return 0;
}

class TestClock final : public core::time::IMonotonicClock {
 public:
  [[nodiscard]] core::time::MonotonicClockProperties
  properties() const noexcept override {
    return {kDomain, std::chrono::microseconds{1},
            std::chrono::microseconds{10},
            std::chrono::microseconds{1}, true};
  }

  [[nodiscard]] core::time::MonotonicClockReading
  read() const noexcept override {
    return {core::time::MonotonicClockStatus::ok,
            kDomain, now, std::chrono::microseconds{1}};
  }

  void advance(const core::time::MonotonicDuration amount) noexcept {
    now += amount;
  }

  core::time::MonotonicTime now{0};
};

class TestDriver final : public transport::ICanDriver,
                         public transport::ICanChannelArbiter {
 public:
  [[nodiscard]] transport::CanPhysicalChannelId
  physical_channel_id() const noexcept override {
    return {1U};
  }
  [[nodiscard]] transport::ICanChannelArbiter&
  channel_arbiter() noexcept override {
    return *this;
  }
  [[nodiscard]] transport::CanDriverExecutionContract
  execution_contract() const noexcept override {
    const auto micros = std::chrono::microseconds{10};
    return {micros, micros, micros, micros,
            micros, micros, micros};
  }
  [[nodiscard]] transport::CanCapabilities
  capabilities() const noexcept override {
    return {true, false, false, false, 8U};
  }
  [[nodiscard]] bool try_acquire(
      const transport::CanPhysicalChannelId channel,
      const void* const owner) noexcept override {
    if (channel.value != 1U || owner == nullptr ||
        (owner_ != nullptr && owner_ != owner)) {
      return false;
    }
    owner_ = owner;
    return true;
  }
  void release(
      const transport::CanPhysicalChannelId channel,
      const void* const owner) noexcept override {
    if (channel.value == 1U && owner_ == owner) {
      owner_ = nullptr;
    }
  }
  [[nodiscard]] transport::CanStatus open(
      const transport::CanChannelConfig& config) noexcept override {
    if (fail_open || !transport::capabilities_support(capabilities(), config)) {
      return transport::CanStatus::io_error;
    }
    ++opens;
    is_open_ = true;
    return transport::CanStatus::ok;
  }
  void close() noexcept override {
    ++closes;
    is_open_ = false;
  }
  [[nodiscard]] bool is_open() const noexcept override {
    return is_open_;
  }
  [[nodiscard]] transport::CanStatus try_send(
      const transport::CanFrame&) noexcept override {
    ++tx_attempts;
    return is_open_ ? transport::CanStatus::ok
                    : transport::CanStatus::not_open;
  }
  [[nodiscard]] transport::CanReceiveResult
  try_receive() noexcept override {
    if (fail_receive) {
      return {transport::CanStatus::io_error, {}, 0U};
    }
    return {is_open_ ? transport::CanStatus::would_block
                     : transport::CanStatus::not_open, {}, 0U};
  }

  bool fail_open{false};
  bool fail_receive{false};
  int opens{0};
  int closes{0};
  int tx_attempts{0};

 private:
  const void* owner_{nullptr};
  bool is_open_{false};
};

// Test-only scripted UDS transport. Physical CAN/ISO-TP behavior is validated
// separately by Core conformance and real DAF SAC Stage 3.3A proof.
class ScriptedTransport final : public transport::IDiagnosticTransport {
 public:
  [[nodiscard]] bool valid() const noexcept override {
    return true;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus start_send(
      const std::byte* payload, const std::size_t length) noexcept override {
    if (payload == nullptr || length != 3U || pending_ || ready_) {
      return transport::DiagnosticTransportStatus::invalid_argument;
    }
    for (std::size_t i = 0U; i < length; ++i) {
      request_[i] = payload[i];
    }
    pending_ = true;
    return transport::DiagnosticTransportStatus::in_progress;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus service(
      const core::time::MonotonicClockReading& reading) noexcept override {
    if (!pending_) {
      return transport::DiagnosticTransportStatus::idle;
    }
    pending_ = false;
    tx_completed_ = reading;
    if (request_[0U] != std::byte{0x22U}) {
      return transport::DiagnosticTransportStatus::ok;
    }
    const auto did = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(request_[1U])) << 8U) |
        std::to_integer<std::uint8_t>(request_[2U]));
    if (drop_did == did) {
      return transport::DiagnosticTransportStatus::ok;
    }
    response_ = {};
    if (reject_did == did) {
      response_[0U] = std::byte{0x7FU};
      response_[1U] = std::byte{0x22U};
      response_[2U] = std::byte{0x31U};
      response_size_ = 3U;
    } else {
      std::string_view value{};
      if (did == daf::kDidVin) {
        value = "XLRSACPROOF000001";
      } else if (did == daf::kDidSoftware) {
        value = "SW-PROOF";
      } else if (did == daf::kDidHardware) {
        value = "HW-PROOF";
      } else {
        return transport::DiagnosticTransportStatus::ok;
      }
      response_[0U] = std::byte{0x62U};
      response_[1U] = request_[1U];
      response_[2U] = request_[2U];
      response_size_ = 3U + value.size();
      for (std::size_t i = 0U; i < value.size(); ++i) {
        response_[3U + i] = std::byte{
            static_cast<unsigned char>(value[i])};
      }
    }
    received_ = reading;
    ready_ = true;
    return transport::DiagnosticTransportStatus::ok;
  }

  [[nodiscard]] bool tx_busy() const noexcept override {
    return pending_;
  }
  [[nodiscard]] transport::DiagnosticTransportStatus
  last_tx_status() const noexcept override {
    return pending_ ? transport::DiagnosticTransportStatus::in_progress
                    : transport::DiagnosticTransportStatus::ok;
  }
  [[nodiscard]] core::time::MonotonicClockReading
  tx_completion_timestamp() const noexcept override {
    return tx_completed_;
  }
  [[nodiscard]] bool has_received() const noexcept override {
    return ready_;
  }
  [[nodiscard]] std::size_t received_size() const noexcept override {
    return ready_ ? response_size_ : 0U;
  }
  [[nodiscard]] transport::DiagnosticTransportStatus take_received(
      std::byte* destination, const std::size_t capacity,
      std::size_t& length,
      core::time::MonotonicClockReading& completion) noexcept override {
    if (!ready_ || destination == nullptr || capacity < response_size_) {
      return transport::DiagnosticTransportStatus::invalid_argument;
    }
    for (std::size_t i = 0U; i < response_size_; ++i) {
      destination[i] = response_[i];
    }
    length = response_size_;
    completion = received_;
    ready_ = false;
    response_size_ = 0U;
    return transport::DiagnosticTransportStatus::ok;
  }
  void reset() noexcept override {
    pending_ = false;
    ready_ = false;
    response_size_ = 0U;
    tx_completed_ = {};
    received_ = {};
  }

  std::uint16_t reject_did{0U};
  std::uint16_t drop_did{0U};

 private:
  std::array<std::byte, 3U> request_{};
  std::array<std::byte, 128U> response_{};
  std::size_t response_size_{0U};
  bool pending_{false};
  bool ready_{false};
  core::time::MonotonicClockReading tx_completed_{};
  core::time::MonotonicClockReading received_{};
};

class Sink final : public transport::ICanFrameSink {
 public:
  void on_can_frame(const transport::ReceivedCanFrame&) noexcept override {
    ++delivered;
  }
  std::uint32_t delivered{0U};
};

[[nodiscard]] dp::ResolvedDutSessionPlan prepare_plan(
    runtime::DutRegistry& duts,
    dp::DutProfileRegistry& profiles,
    const daf::CanBitrateProfile bitrate) {
  dp::ResolvedDutSessionPlan plan{};
  const auto def = daf::make_profile_definition(bitrate);
  if (profiles.register_profile(def) != dp::ProfileRegistrationStatus::registered ||
      !profiles.freeze_configuration()) {
    return plan;
  }
  const auto selected = profiles.select(daf::profile_id(bitrate));
  if (selected.status != dp::ProfileSelectionStatus::selected) {
    return plan;
  }
  const auto dut = duts.register_dut(selected.profile->dut);
  if (dut.status != runtime::DutRegistrationStatus::registered ||
      !duts.freeze_configuration()) {
    return plan;
  }
  dp::DutProfileBinding binding{};
  binding.session_owner = 700U;
  binding.dut = dut.handle;
  binding.timestamp_domain = kDomain;
  binding.resources[0U] = {daf::kPrimaryCanRole, kPhysicalCan};
  binding.resource_count = 1U;
  if (dp::resolve_profile_session(*selected.profile, duts, binding, plan) !=
      dp::ProfileResolveStatus::resolved) {
    plan = {};
  }
  return plan;
}

[[nodiscard]] bool prepare_bus(
    transport::CanBusRuntime& bus,
    Sink& sink) noexcept {
  transport::CanFilter filter{};
  filter.identifier = daf::kResponseCanId;
  filter.mask = 0x1FFFFFFFU;
  filter.match_standard = false;
  filter.match_extended = true;
  const auto subscribed = bus.subscribe(
      filter, sink, {std::chrono::microseconds{10}});
  return subscribed.status == transport::CanSubscriptionStatus::subscribed &&
         bus.freeze_configuration();
}

[[nodiscard]] uds::UdsClientConfig make_uds_config() noexcept {
  uds::UdsClientConfig config{};
  config.timing.p2 = std::chrono::milliseconds{100};
  config.timing.p2_star = std::chrono::milliseconds{5000};
  config.timestamp_domain = kDomain;
  config.max_timestamp_uncertainty = std::chrono::microseconds{10};
  return config;
}

struct Fixture {
  explicit Fixture(
      const daf::CanBitrateProfile bitrate =
          daf::CanBitrateProfile::k250k)
      : plan(prepare_plan(duts, profiles, bitrate)),
        bus(driver),
        bus_ready(prepare_bus(bus, sink)),
        uds_client(script, make_uds_config()),
        program(bitrate, uds_client, clock,
                {std::chrono::milliseconds{1},
                 std::chrono::milliseconds{1},
                 std::chrono::milliseconds{1},
                 std::chrono::milliseconds{1},
                 std::chrono::milliseconds{1}}),
        profile_endpoint(plan, program),
        endpoint(plan, bus, profile_endpoint, program,
                 driver.execution_contract()),
        session(resources, duts, endpoint, nullptr, nullptr),
        host(session, clock),
        application(plan, endpoint, session, host, program, clock) {}

  TestClock clock{};
  TestDriver driver{};
  runtime::ResourceManager resources{};
  runtime::DutRegistry duts{};
  dp::DutProfileRegistry profiles{};
  dp::ResolvedDutSessionPlan plan{};
  transport::CanBusRuntime bus;
  Sink sink{};
  bool bus_ready{false};
  ScriptedTransport script{};
  uds::UdsClient uds_client;
  daf::IdentificationProgram program;
  dp::DutProfileSessionEndpoint profile_endpoint;
  app::BenchEndpoint endpoint;
  bench::BenchSession session;
  bench::BenchSessionHostRuntime host;
  app::Application application;
};

int run_to_end(Fixture& fixture) {
  for (int i = 0; i < 80; ++i) {
    fixture.clock.advance(std::chrono::milliseconds{1});
    const auto status = fixture.application.service();
    if (status == app::AppStatus::ok &&
        fixture.application.snapshot().state == app::AppState::identified) {
      return 0;
    }
    if (status != app::AppStatus::no_action &&
        status != app::AppStatus::ok) {
      return 1;
    }
  }
  return 1;
}

}  // namespace

int main() {
  int failures = 0;
  failures += require(app::operation_available(app::AppOperation::identify) &&
      !app::operation_available(app::AppOperation::actuator_test) &&
      !app::operation_available(app::AppOperation::read_dtc) &&
      !app::operation_available(app::AppOperation::program_ecu),
      "only identification function is enabled");

  {
    Fixture f;
    failures += require(f.bus_ready && f.endpoint.valid() &&
        f.application.configure({std::chrono::milliseconds{250}}),
        "application profile and Bench session configure");
    failures += require(f.application.identify() == app::AppStatus::ok &&
        f.session.state() == bench::BenchSessionState::running &&
        f.resources.active_count() == 2U && f.driver.opens == 1,
        "Bench owns ECU and CAN resources before CAN opens");
    failures += require(run_to_end(f) == 0,
        "real UDS identification program completes through Bench service");
    const auto snapshot = f.application.snapshot();
    failures += require(snapshot.identification_available &&
        std::string_view{snapshot.vin_suffix.data()} == "0001" &&
        snapshot.bench.state == bench::BenchSessionState::ready &&
        snapshot.bench.active_resource_count == 0U &&
        f.resources.active_count() == 0U &&
        !f.driver.is_open() && f.driver.closes == 1,
        "completed request retains record but safely closes CAN and releases leases");
    failures += require(f.application.identification().vin.view() ==
        "XLRSACPROOF000001" &&
        f.application.identification().software.view() == "SW-PROOF" &&
        f.application.identification().hardware.view() == "HW-PROOF",
        "three DID values are available to authorized app layer");
    failures += require(f.application.identify() == app::AppStatus::ok &&
        run_to_end(f) == 0 && f.driver.opens == 2 && f.driver.closes == 2,
        "successive runs do not leak resources");
  }

  {
    Fixture f;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::ok &&
        f.application.stop() == app::AppStatus::ok &&
        f.resources.active_count() == 0U &&
        !f.driver.is_open() &&
        !f.application.snapshot().identification_available,
        "user stop closes CAN and does not invent a completed result");
  }

  {
    Fixture f;
    f.script.reject_did = daf::kDidSoftware;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::ok,
        "NRC fixture starts");
    for (int i = 0; i < 40 &&
             f.application.snapshot().state == app::AppState::identifying; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(f.application.snapshot().state == app::AppState::faulted &&
        f.application.snapshot().nrc == 0x31U &&
        !f.driver.is_open() && f.resources.active_count() == 0U,
        "NRC preserved and failed session tears down");
    failures += require(f.application.recover() == app::AppStatus::ok &&
        f.application.snapshot().state == app::AppState::ready,
        "explicit recovery is possible after a clean fault teardown");
  }

  {
    Fixture f;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::ok,
        "watchdog fixture starts");
    f.clock.advance(std::chrono::milliseconds{300});
    failures += require(f.application.service() == app::AppStatus::timeout &&
        f.resources.active_count() == 0U &&
        !f.driver.is_open() &&
        f.application.snapshot().state == app::AppState::faulted,
        "missed host deadline fails closed and retains timeout status");
  }

  {
    Fixture f;
    const auto conflict = f.resources.acquire(kPhysicalCan, 999U);
    failures += require(conflict.status == runtime::ResourceAcquireStatus::acquired &&
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::resource_unavailable &&
        f.driver.opens == 0 &&
        f.resources.active_count() == 1U,
        "resource conflict stops before physical CAN open");
    failures += require(
        f.resources.release(conflict.lease) ==
            runtime::ResourceReleaseStatus::released &&
        f.application.recover() == app::AppStatus::ok &&
        f.application.identify() == app::AppStatus::ok &&
        f.application.stop() == app::AppStatus::ok,
        "resource contention recovery and restart");
  }

  {
    Fixture f;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::ok,
        "bus fault fixture starts");
    f.driver.fail_receive = true;
    failures += require(f.application.service() == app::AppStatus::runtime_fault &&
        f.application.snapshot().can_status == transport::CanStatus::io_error &&
        f.resources.active_count() == 0U && !f.driver.is_open(),
        "CAN IO error is preserved and Bench always releases resources");
    f.driver.fail_receive = false;
    failures += require(f.application.recover() == app::AppStatus::ok &&
        f.application.identify() == app::AppStatus::ok &&
        run_to_end(f) == 0,
        "CAN fault can recover and identify again");
  }

  {
    Fixture f;
    f.script.drop_did = daf::kDidVin;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::ok,
        "absolute operation timeout fixture starts");
    f.clock.advance(std::chrono::seconds{11});
    failures += require(f.application.service() == app::AppStatus::timeout &&
        f.resources.active_count() == 0U && !f.driver.is_open() &&
        f.application.snapshot().state == app::AppState::faulted,
        "absolute 10 second operation deadline cannot run indefinitely");
  }

  {
    Fixture f;
    f.driver.fail_open = true;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::runtime_fault &&
        f.resources.active_count() == 0U && !f.driver.is_open(),
        "CAN open rejection releases all Bench resource leases");
    f.driver.fail_open = false;
    failures += require(f.application.recover() == app::AppStatus::ok &&
        f.application.identify() == app::AppStatus::ok &&
        run_to_end(f) == 0,
        "CAN open failure can be recovered and retried");
  }

  {
    Fixture f{daf::CanBitrateProfile::k500k};
    failures += require(f.bus_ready && !f.endpoint.valid() &&
        !f.application.configure({std::chrono::milliseconds{250}}) &&
        f.driver.opens == 0,
        "unverified 500k profile is not enabled for first application");
  }

  if (failures == 0) {
    std::cout << "DAF_SAC_APPLICATION_TESTS=PASS\n";
  }
  return failures == 0 ? 0 : 1;
}
