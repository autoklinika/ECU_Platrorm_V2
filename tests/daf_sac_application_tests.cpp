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
    rx_read_ = rx_write_ = 0U;
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
    if (!is_open_) {
      return {transport::CanStatus::not_open, {}, 0U};
    }
    if (rx_read_ < rx_write_) {
      return {transport::CanStatus::ok, frames_[rx_read_++], 0U};
    }
    return {transport::CanStatus::would_block, {}, 0U};
  }

  bool inject(const transport::CanFrame& frame,
              const core::time::MonotonicClockReading reading) noexcept {
    if (!is_open_ || rx_write_ >= frames_.size()) {
      return false;
    }
    frames_[rx_write_++] = {frame, reading};
    return true;
  }

  bool fail_open{false};
  bool fail_receive{false};
  int opens{0};
  int closes{0};
  int tx_attempts{0};

 private:
  const void* owner_{nullptr};
  bool is_open_{false};
  std::array<transport::ReceivedCanFrame, 16U> frames_{};
  std::size_t rx_read_{0U};
  std::size_t rx_write_{0U};
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
    if (payload == nullptr || length < 2U || length > 4U ||
        pending_ || ready_) {
      return transport::DiagnosticTransportStatus::invalid_argument;
    }
    ++requests;
    last_request_length = length;
    last_request_sid = std::to_integer<std::uint8_t>(payload[0U]);
    for (std::size_t i = 0U; i < length; ++i) {
      request_[i] = payload[i];
    }
    pending_ = true;
    return transport::DiagnosticTransportStatus::in_progress;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus service(
      const core::time::MonotonicClockReading& reading) noexcept override {
    if (!pending_) {
      if (delayed_reply_ && reading.value >= reply_ready_at_) {
        delayed_reply_ = false;
        received_ = reading;
        ready_ = true;
        return transport::DiagnosticTransportStatus::ok;
      }
      return transport::DiagnosticTransportStatus::idle;
    }
    pending_ = false;
    tx_completed_ = reading;
    if (request_[0U] != std::byte{0x22U}) {
      response_ = {};
      const auto sid = std::to_integer<std::uint8_t>(request_[0U]);
      if (sid == 0x10U && request_[1U] == std::byte{0x03U}) {
        ++session_requests;
        response_[0U] = std::byte{0x50U};
        response_[1U] = std::byte{0x03U};
        response_size_ = 2U;
        if (announce_short_session_timing) {
          // Physical SAC reply: 50 03 00 19 00 C8 => P2 25ms / P2* 2s.
          response_[2U] = std::byte{0x00U};
          response_[3U] = std::byte{0x19U};
          response_[4U] = std::byte{0x00U};
          response_[5U] = std::byte{0xC8U};
          response_size_ = 6U;
        }
      } else if (sid == 0x19U && request_[1U] == std::byte{0x02U}) {
        ++dtc_requests;
        last_mask = std::to_integer<std::uint8_t>(request_[2U]);
        response_[0U] = std::byte{0x59U};
        response_[1U] = std::byte{0x02U};
        response_[2U] = std::byte{0xFFU};
        response_[3U] = std::byte{0x12U};
        response_[4U] = std::byte{0x34U};
        response_[5U] = std::byte{0x56U};
        response_[6U] = std::byte{0x2FU};
        response_size_ = malformed_dtc ? 6U : 7U;
        if (overflow_dtc) {
          // 129 well-formed entries, intentionally exceeding the
          // product-level fixed record capacity of 128.
          response_size_ = 3U + 129U * 4U;
          for (std::size_t i = 0U; i < 129U; ++i) {
            const auto offset = 3U + i * 4U;
            response_[offset] = std::byte{0x12U};
            response_[offset + 1U] = std::byte{0x34U};
            response_[offset + 2U] = std::byte{0x56U};
            response_[offset + 3U] = std::byte{0x2FU};
          }
        }
      } else if (sid == 0x14U && last_request_length == 4U) {
        ++clear_requests;
        response_[0U] = std::byte{0x54U};
        response_size_ = 1U;
      } else {
        return transport::DiagnosticTransportStatus::ok;
      }
      if (reject_service == sid) {
        response_[0U] = std::byte{0x7FU};
        response_[1U] = static_cast<std::byte>(sid);
        response_[2U] = std::byte{0x31U};
        response_size_ = 3U;
      }
      if (drop_service == sid) {
        return transport::DiagnosticTransportStatus::ok;
      }
      if (sid == 0x10U && drop_next_post_clear_session &&
          clear_requests != 0) {
        // A response can be visible on a parallel CAN sniffer while the
        // UDS client fails to complete its first post-clear 10 03 exchange.
        drop_next_post_clear_session = false;
        return transport::DiagnosticTransportStatus::ok;
      }
      if (sid == 0x19U && dtc_reply_delay.count() > 0) {
        delayed_reply_ = true;
        reply_ready_at_ = reading.value + dtc_reply_delay;
        return transport::DiagnosticTransportStatus::ok;
      }
      if (sid == 0x14U && clear_reply_delay.count() > 0) {
        delayed_reply_ = true;
        reply_ready_at_ = reading.value + clear_reply_delay;
        return transport::DiagnosticTransportStatus::ok;
      }
      received_ = reading;
      ready_ = true;
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
      } else if (did == daf::kDidVoltage) {
        response_[0U] = std::byte{0x62U};
        response_[1U] = std::byte{0xFEU};
        response_[2U] = std::byte{0x96U};
        response_[7U] = invalid_voltage_value
            ? std::byte{0xFEU} : std::byte{0x00U};
        response_[8U] = invalid_voltage_value
            ? std::byte{0xFEU} : std::byte{0xFAU}; // 25.0 V
        response_[9U] = std::byte{0x00U};
        response_[10U] = std::byte{0xEFU}; // 23.9 V
        response_size_ = malformed_voltage ? 10U : 11U;
        received_ = reading;
        ready_ = true;
        return transport::DiagnosticTransportStatus::ok;
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
      if (did == daf::kDidVin && blank_vin_ff17) {
        response_size_ = 20U;
        for (std::size_t i = 3U; i < response_size_; ++i) {
          response_[i] = std::byte{255U};
        }
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
    delayed_reply_ = false;
    reply_ready_at_ = {};
    response_size_ = 0U;
    tx_completed_ = {};
    received_ = {};
  }

  std::uint16_t reject_did{0U};
  std::uint16_t drop_did{0U};
  std::uint8_t reject_service{0U};
  std::uint8_t drop_service{0U};
  std::uint8_t last_request_sid{0U};
  std::uint8_t last_mask{0U};
  std::size_t last_request_length{0U};
  bool malformed_dtc{false};
  bool overflow_dtc{false};
  bool blank_vin_ff17{false};
  bool malformed_voltage{false};
  bool invalid_voltage_value{false};
  bool announce_short_session_timing{false};
  bool drop_next_post_clear_session{false};
  std::chrono::milliseconds dtc_reply_delay{0};
  std::chrono::milliseconds clear_reply_delay{0};
  int requests{0};
  int clear_requests{0};
  int dtc_requests{0};
  int session_requests{0};

 private:
  std::array<std::byte, 4U> request_{};
  std::array<std::byte, 1024U> response_{};
  std::size_t response_size_{0U};
  bool pending_{false};
  bool ready_{false};
  bool delayed_reply_{false};
  core::time::MonotonicTime reply_ready_at_{0};
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

[[nodiscard]] bool prepare_services_bus(
    transport::CanBusRuntime& bus, Sink& uds_sink,
    daf::PressureMonitor& pressure) noexcept {
  transport::CanFilter uds_filter{};
  uds_filter.identifier = daf::kResponseCanId;
  uds_filter.mask = 0x1FFFFFFFU;
  uds_filter.match_standard = false;
  uds_filter.match_extended = true;
  transport::CanFilter pressure_filter{};
  pressure_filter.identifier = daf::kPressureCanId;
  // Include DP/PF/PS/source, exclude the J1939 priority.
  pressure_filter.mask = daf::kPressureCanMask;
  pressure_filter.match_standard = false;
  pressure_filter.match_extended = true;
  return bus.subscribe(
             uds_filter, uds_sink, {std::chrono::microseconds{10}}).status ==
             transport::CanSubscriptionStatus::subscribed &&
         bus.subscribe(
             pressure_filter, pressure,
             {std::chrono::microseconds{10}}).status ==
             transport::CanSubscriptionStatus::subscribed &&
         bus.freeze_configuration();
}

[[nodiscard]] transport::CanFrame pressure_frame(
    const std::uint8_t first, const std::uint8_t second,
    const std::uint32_t id = 0x18FEAE30U) {
  transport::CanFrame frame{};
  frame.identifier = id;
  frame.identifier_format = transport::CanIdentifierFormat::extended_29_bit;
  frame.format = transport::CanFrameFormat::classic;
  frame.type = transport::CanFrameType::data;
  frame.length = 8U;
  frame.payload[2U] = static_cast<std::byte>(first);
  frame.payload[3U] = static_cast<std::byte>(second);
  return frame;
}

struct ServicesFixture {
  explicit ServicesFixture(
      const daf::CanBitrateProfile bitrate = daf::CanBitrateProfile::k250k)
      : plan(prepare_plan(duts, profiles, bitrate)),
        bus(driver),
        bus_ready(prepare_services_bus(bus, uds_sink, pressure)),
        uds_client(script, make_uds_config()),
        services(bitrate, uds_client, clock,
                 {std::chrono::milliseconds{1},
                  std::chrono::milliseconds{1},
                  std::chrono::milliseconds{1},
                  std::chrono::milliseconds{1},
                  std::chrono::milliseconds{1}}),
        profile_endpoint(plan, services),
        endpoint(plan, bus, profile_endpoint, services,
                 driver.execution_contract()),
        session(resources, duts, endpoint, nullptr, nullptr),
        host(session, clock),
        application(plan, endpoint, session, host, services,
                    pressure, clock) {}

  TestClock clock{};
  TestDriver driver{};
  runtime::ResourceManager resources{};
  runtime::DutRegistry duts{};
  dp::DutProfileRegistry profiles{};
  dp::ResolvedDutSessionPlan plan{};
  transport::CanBusRuntime bus;
  Sink uds_sink{};
  daf::PressureMonitor pressure{};
  bool bus_ready{false};
  ScriptedTransport script{};
  uds::UdsClient uds_client;
  daf::ServiceProgram services;
  dp::DutProfileSessionEndpoint profile_endpoint;
  app::BenchEndpoint endpoint;
  bench::BenchSession session;
  bench::BenchSessionHostRuntime host;
  app::Application application;
};

[[nodiscard]] int service_to_end(
    ServicesFixture& f, const app::AppState expected,
    const int limit = 150) {
  for (int i = 0; i < limit; ++i) {
    f.clock.advance(std::chrono::milliseconds{1});
    const auto status = f.application.service();
    const auto state = f.application.snapshot().state;
    if (state == expected && status == app::AppStatus::ok) {
      return 0;
    }
    if (state == app::AppState::faulted ||
        (status != app::AppStatus::ok &&
         status != app::AppStatus::no_action)) {
      return 1;
    }
  }
  return 1;
}

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
      app::operation_available(app::AppOperation::read_dtc) &&
      !app::operation_available(app::AppOperation::clear_dtc) &&
      app::operation_available(app::AppOperation::live_parameters) &&
      !app::operation_available(app::AppOperation::actuator_test) &&
      !app::operation_available(app::AppOperation::program_ecu) &&
      app::kOperationCatalog[1U].physically_validated &&
      !app::kOperationCatalog[2U].physically_validated &&
      !app::kOperationCatalog[3U].physically_validated,
      "SAC DTC read validated on both DUTs; destructive clear and pressure remain unverified");

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
        !snapshot.vin_unprogrammed &&
        snapshot.schema_version == app::AppSnapshot::kSchemaVersion &&
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
    failures += require(f.bus_ready && f.endpoint.valid() &&
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.snapshot().bitrate == 500000U &&
        f.application.snapshot().profile_id == 0xDAF00050U &&
        f.application.identify() == app::AppStatus::ok &&
        run_to_end(f) == 0 &&
        f.resources.active_count() == 0U && !f.driver.is_open(),
        "500k DUT profile performs independent read-only identification");
  }

  {
    Fixture f{daf::CanBitrateProfile::k500k};
    f.script.blank_vin_ff17 = true;
    failures += require(
        f.bus_ready && f.endpoint.valid() &&
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.identify() == app::AppStatus::ok &&
        run_to_end(f) == 0 &&
        f.application.identification().vin_unprogrammed_ff17 &&
        f.application.identification().vin.view().empty() &&
        f.application.snapshot().identification_available &&
        f.application.snapshot().vin_unprogrammed &&
        f.application.snapshot().schema_version ==
            app::AppSnapshot::kSchemaVersion &&
        f.application.snapshot().vin_suffix ==
            std::array<char, 5U>{} &&
        f.resources.active_count() == 0U,
        "500k FF17 VIN leaves GUI suffix empty while other identity is usable");
  }

  {
    ServicesFixture f{daf::CanBitrateProfile::k500k};
    failures += require(
        f.bus_ready && f.endpoint.valid() &&
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.snapshot().bitrate == 500000U &&
        f.application.snapshot().profile_id == 0xDAF00050U &&
        f.application.read_parameters() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::parameters_ready, 900) == 0 &&
        f.application.voltage().valid &&
        f.resources.active_count() == 0U,
        "500k SAC independently reads voltage without changing Core or Bench");
    failures += require(
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0 &&
        f.application.dtcs().valid &&
        f.application.dtcs().count == 1U &&
        f.script.session_requests == 1 &&
        f.script.dtc_requests == 1 &&
        f.resources.active_count() == 0U &&
        !f.driver.is_open(),
        "500k SAC independently reads DTC 19 02 FF in extended session");
    failures += require(
        f.application.prepare_clear_dtcs().sequence == 0U &&
        f.application.clear_dtcs({1U, f.plan.profile_id, 1U}, true) ==
            app::AppStatus::confirmation_required &&
        f.script.clear_requests == 0,
        "unverified 500k SAC cannot transmit 0x14 even with synthetic confirmation");
  }

  {
    // Pressure decoding is independent of UDS and rejects SAE J1939
    // reserved/NA encodings, unknown source and wrong frame size.
    daf::PressureMonitor p;
    transport::ReceivedCanFrame value{};
    value.frame = pressure_frame(125U, 25U);
    value.timestamp = {core::time::MonotonicClockStatus::ok,
                       kDomain, core::time::MonotonicTime{0},
                       core::time::MonotonicDuration{1}};
    p.on_can_frame(value);
    failures += require(p.sample().received &&
        p.sample().pressure1_valid && p.sample().pressure2_valid &&
        p.sample().pressure1_bar == 10.0F &&
        p.sample().pressure2_bar == 2.0F,
        "J1939 SAC pressure scaled from two independent octets");

    p.reset();
    value.frame = pressure_frame(0xFEU, 0xFFU);
    p.on_can_frame(value);
    failures += require(p.sample().received &&
        !p.sample().pressure1_valid && !p.sample().pressure2_valid,
        "physical 0xFE/0xFF placeholder cannot become fictitious pressure");

    p.reset();
    value.frame = pressure_frame(125U, 25U, 0x18FEAE99U);
    p.on_can_frame(value);
    value.frame = pressure_frame(125U, 25U);
    value.frame.length = 3U;
    p.on_can_frame(value);
    failures += require(!p.sample().received,
        "unrelated sender and truncated pressure packets are ignored");
  }

  {
    ServicesFixture f;
    failures += require(f.bus_ready && f.endpoint.valid() &&
        f.application.configure({std::chrono::milliseconds{250}}),
        "Stage 4.2 service application configures with dual subscribers");
    failures += require(f.application.read_parameters() == app::AppStatus::ok &&
        f.resources.active_count() == 2U &&
        f.driver.inject(pressure_frame(125U, 25U), f.clock.read()),
        "voltage request and passive PGN capture share Bench session");
    failures += require(
        service_to_end(f, app::AppState::parameters_ready) == 0 &&
        f.application.voltage().valid &&
        f.application.voltage().permanent_v == 25.0F &&
        f.application.voltage().ignition_v > 23.8F &&
        f.application.voltage().ignition_v < 24.0F &&
        f.application.pressure().pressure1_valid &&
        f.application.pressure().pressure1_bar == 10.0F &&
        f.application.pressure().pressure2_bar == 2.0F &&
        f.resources.active_count() == 0U && !f.driver.is_open(),
        "UDS 22 FE96 and passive pressure return without leaking resources");
    failures += require(
        f.application.snapshot().voltage_available &&
        f.application.snapshot().pressure1_valid &&
        f.application.snapshot().pressure2_valid,
        "parameter availability is separately exposed");
    failures += require(f.application.read_parameters() == app::AppStatus::ok &&
        f.driver.inject(pressure_frame(0xFEU, 0xFEU), f.clock.read()) &&
        service_to_end(f, app::AppState::parameters_ready) == 0 &&
        f.application.pressure().received &&
        !f.application.pressure().pressure1_valid &&
        !f.application.pressure().pressure2_valid &&
        f.application.voltage().valid,
        "invalid pressure remains unavailable while voltage read succeeds");
    failures += require(
        f.application.identify() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::identified) == 0 &&
        f.application.snapshot().identification_available,
        "first ECU identification also operates through unified service program");
  }

  {
    ServicesFixture f;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs(0xAAU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "legacy SAC session 10 03 then UDS 19 02 reads DTC list");
    failures += require(f.script.session_requests == 1 &&
        f.script.dtc_requests == 1 &&
        f.script.last_mask == 0xAAU &&
        f.application.dtcs().valid &&
        f.application.dtcs().requested_mask == 0xAAU &&
        f.application.dtcs().status_availability == 0xFFU &&
        f.application.dtcs().count == 1U &&
        f.application.dtcs().records[0U].code == 0x123456U &&
        f.application.dtcs().records[0U].status == 0x2FU &&
        f.resources.active_count() == 0U,
        "read-DTC result includes status, availability and bounded records");
    failures += require(f.script.clear_requests == 0,
        "reading faults never causes implicit erase-DTC command");
    failures += require(
        f.application.prepare_clear_dtcs().sequence == 0U &&
        f.application.clear_dtcs({1U, f.plan.profile_id, 1U}, true) ==
            app::AppStatus::confirmation_required &&
        f.script.clear_requests == 0,
        "filtered DTC status-mask read cannot authorize clearing all groups");
    failures += require(
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0 &&
        f.application.dtcs().requested_mask == 0xFFU,
        "fresh full-mask DTC inventory is required before erase");

    const auto challenge = f.application.prepare_clear_dtcs();
    failures += require(challenge.sequence != 0U &&
        challenge.profile_id == daf::profile_id(daf::CanBitrateProfile::k250k) &&
        challenge.inspected_dtc_count == 1U,
        "clear action requires a preceding successful DTC inspection");
    failures += require(
        f.application.clear_dtcs(challenge, false) ==
            app::AppStatus::confirmation_required &&
        f.application.clear_dtcs(
            {challenge.sequence + 1U, challenge.profile_id, challenge.inspected_dtc_count},
            true) == app::AppStatus::confirmation_required &&
        f.script.clear_requests == 0,
        "missing operator confirmation or stale challenge cannot transmit");
    failures += require(f.application.clear_dtcs(challenge, true) ==
        app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_clear_acknowledged) == 0 &&
        f.script.session_requests == 3 && f.script.dtc_requests == 2 &&
        f.script.clear_requests == 1 &&
        f.script.last_request_sid == 0x14U &&
        f.script.last_request_length == 4U &&
        f.application.snapshot().clear_acknowledged &&
        !f.application.snapshot().dtcs_available &&
        f.resources.active_count() == 0U && !f.driver.is_open(),
        "one-time confirmed UDS 14 FFFFFF receives 54 ACK; old DTC list invalidated");
    failures += require(f.application.clear_dtcs(challenge, true) ==
        app::AppStatus::confirmation_required && f.script.clear_requests == 1,
        "clear confirmation cannot be replayed");
  }

  {
    ServicesFixture f;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.clear_dtcs({1U, f.plan.profile_id, 0U}, true) ==
            app::AppStatus::confirmation_required &&
        f.script.requests == 0,
        "DTC erase impossible before read and explicit challenge");
  }

  {
    ServicesFixture f;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "full-mask inventory can be inspected before TTL expires");
    const auto challenge = f.application.prepare_clear_dtcs();
    f.clock.advance(std::chrono::seconds{181});
    failures += require(
        challenge.sequence != 0U &&
        f.application.prepare_clear_dtcs().sequence == 0U &&
        f.application.clear_dtcs(challenge, true) ==
            app::AppStatus::confirmation_required &&
        f.script.clear_requests == 0,
        "expired 180-second inventory and its issued challenge cannot erase DTCs");
    failures += require(
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0 &&
        f.application.prepare_clear_dtcs().sequence != 0U,
        "expired inventory can be refreshed without automatic DTC clearing");
  }

  {
    ServicesFixture f;
    f.script.announce_short_session_timing = true;
    f.script.dtc_reply_delay = std::chrono::milliseconds{200};
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready, 350) == 0 &&
        f.application.dtcs().count == 1U &&
        f.script.dtc_requests == 1 &&
        f.script.clear_requests == 0 &&
        f.resources.active_count() == 0U,
        "SAC advertised 25ms P2 does not cause false timeout on a 200ms DTC reply");
  }

  {
    ServicesFixture f;
    f.script.reject_service = 0x19U;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok,
        "NRC on DTC request fixture starts");
    for (int i = 0; i < 40 &&
             f.application.snapshot().state == app::AppState::reading_dtcs; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(f.application.snapshot().state == app::AppState::faulted &&
        f.application.snapshot().nrc == 0x31U &&
        !f.application.snapshot().dtcs_available &&
        !f.driver.is_open() && f.resources.active_count() == 0U,
        "UDS NRC is preserved and Bench tears down read-DTC failure");
    f.script.reject_service = 0U;
    failures += require(f.application.recover() == app::AppStatus::ok &&
        f.application.read_dtcs() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "DTC read recovers without hard-coded Core changes");
  }

  {
    ServicesFixture f;
    f.script.overflow_dtc = true;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok,
        "overcapacity DTC fixture starts");
    for (int i = 0; i < 45 &&
             f.application.snapshot().state == app::AppState::reading_dtcs; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(f.application.snapshot().state == app::AppState::faulted &&
        !f.application.snapshot().dtcs_available &&
        f.resources.active_count() == 0U,
        "129-entry response does not exceed 128-entry bounded SAC DTC capacity");
  }

  {
    ServicesFixture f;
    f.script.malformed_dtc = true;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok,
        "malformed DTC fixture starts");
    for (int i = 0; i < 40 &&
             f.application.snapshot().state == app::AppState::reading_dtcs; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(f.application.snapshot().state == app::AppState::faulted &&
        !f.application.snapshot().dtcs_available &&
        f.resources.active_count() == 0U,
        "partial 3-byte DTC record is rejected fail-closed");
  }

  {
    ServicesFixture f;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_parameters() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::parameters_ready, 900) == 0 &&
        f.application.voltage().valid &&
        !f.application.pressure().received &&
        !f.application.snapshot().pressure1_valid &&
        !f.application.snapshot().pressure2_valid &&
        f.resources.active_count() == 0U,
        "bounded pressure wait permits voltage-only result without invented data");
  }

  {
    ServicesFixture f;
    f.script.invalid_voltage_value = true;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_parameters() == app::AppStatus::ok,
        "voltage out-of-range fixture starts");
    for (int i = 0; i < 40 &&
             f.application.snapshot().state == app::AppState::reading_parameters; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(
        f.application.snapshot().state == app::AppState::faulted &&
        !f.application.snapshot().voltage_available &&
        f.resources.active_count() == 0U,
        "unavailable FEFE voltage value rejected before application reporting");
  }

  {
    ServicesFixture f;
    f.script.malformed_voltage = true;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_parameters() == app::AppStatus::ok,
        "short voltage fixture starts");
    for (int i = 0; i < 30 &&
             f.application.snapshot().state == app::AppState::reading_parameters; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(f.application.snapshot().state == app::AppState::faulted &&
        !f.application.snapshot().voltage_available &&
        f.resources.active_count() == 0U,
        "incorrect FE96 field length rejected");
  }

  {
    ServicesFixture f;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "DTC pre-read for abort-before-clear");
    const auto challenge = f.application.prepare_clear_dtcs();
    failures += require(
        f.application.clear_dtcs(challenge, true) == app::AppStatus::ok &&
        f.application.stop() == app::AppStatus::ok &&
        f.script.clear_requests == 0 &&
        f.resources.active_count() == 0U &&
        !f.application.snapshot().clear_acknowledged,
        "operator stop before service never transmits pending erase");
  }

  {
    ServicesFixture f;
    f.script.reject_service = 0x14U;
    failures += require(f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "DTC read before NRC clear fixture");
    const auto challenge = f.application.prepare_clear_dtcs();
    failures += require(f.application.clear_dtcs(challenge, true) ==
        app::AppStatus::ok,
        "explicit DTC clear allowed after accepted read");
    for (int i = 0; i < 40 &&
             f.application.snapshot().state == app::AppState::clearing_dtcs; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(f.application.snapshot().state == app::AppState::faulted &&
        f.application.snapshot().nrc == 0x31U &&
        !f.application.snapshot().clear_acknowledged &&
        f.resources.active_count() == 0U,
        "negative UDS 14 response cannot be misreported as erased");
  }

  {
    ServicesFixture f;
    f.script.clear_reply_delay = std::chrono::milliseconds{650};
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "slow clear simulator: DTC inventory before clear");
    const auto confirmation = f.application.prepare_clear_dtcs();
    failures += require(
        f.application.clear_dtcs(confirmation, true) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_clear_acknowledged, 1000) == 0 &&
        f.script.clear_requests == 1 &&
        f.application.snapshot().clear_acknowledged &&
        f.uds_client.timing().p2 == std::chrono::milliseconds{100} &&
        f.resources.active_count() == 0U,
        "delayed 0x54 at 650 ms accepted; single clear; P2 reset after cleanup");
  }

  {
    ServicesFixture f;
    f.script.announce_short_session_timing = true;
    f.script.drop_next_post_clear_session = true;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "SAC post-clear verification fault simulation has pre-clear DTC evidence");
    const auto challenge = f.application.prepare_clear_dtcs();
    failures += require(
        f.application.clear_dtcs(challenge, true) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_clear_acknowledged) == 0 &&
        f.script.clear_requests == 1,
        "SAC accepts precisely one clear before transient post-clear read fault");
    failures += require(
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready, 1400) != 0 &&
        f.application.snapshot().state == app::AppState::faulted &&
        f.application.snapshot().uds_status == uds::UdsStatus::timeout_p2 &&
        f.resources.active_count() == 0U,
        "first post-clear 10 03 can time out while preserving safe shutdown");
    failures += require(
        f.application.recover() == app::AppStatus::ok &&
        f.application.read_dtcs(0xFFU) == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0 &&
        f.application.dtcs().valid &&
        f.script.clear_requests == 1 &&
        f.resources.active_count() == 0U,
        "a separate read-only retry recovers verification without re-erasing");
  }

  {
    ServicesFixture f;
    f.script.drop_service = 0x14U;
    failures += require(
        f.application.configure({std::chrono::milliseconds{250}}) &&
        f.application.read_dtcs() == app::AppStatus::ok &&
        service_to_end(f, app::AppState::dtcs_ready) == 0,
        "no clear reply simulator: DTC inventory before clear");
    const auto confirmation = f.application.prepare_clear_dtcs();
    failures += require(
        f.application.clear_dtcs(confirmation, true) == app::AppStatus::ok,
        "no clear reply simulator: operator confirmed");
    for (int i = 0; i < 3400 &&
             f.application.snapshot().state == app::AppState::clearing_dtcs; ++i) {
      f.clock.advance(std::chrono::milliseconds{1});
      (void)f.application.service();
    }
    failures += require(
        f.application.snapshot().state == app::AppState::faulted &&
        f.application.snapshot().status == app::AppStatus::clear_outcome_unknown &&
        f.application.snapshot().uds_status == uds::UdsStatus::timeout_p2 &&
        f.script.clear_requests == 1 &&
        f.resources.active_count() == 0U &&
        !f.application.snapshot().clear_acknowledged &&
        f.uds_client.timing().p2 == std::chrono::milliseconds{100},
        "no 0x54 even after 3000 ms: fail closed, no retry, retain P2 diagnosis");
  }

  if (failures == 0) {
    std::cout << "DAF_SAC_APPLICATION_TESTS=PASS\n";
  }
  return failures == 0 ? 0 : 1;
}
