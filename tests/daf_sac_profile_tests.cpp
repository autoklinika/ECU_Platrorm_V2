#include "ecu/core_v2/protocol/uds/uds_client.hpp"
#include "ecu/dut_profile/registry.hpp"
#include "ecu/dut_profile/runtime.hpp"
#include "ecu/dut_profiles/daf_sac/identification_program.hpp"
#include "ecu/dut_profiles/daf_sac/profile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

namespace bench = ecu::bench;
namespace core = ecu::core::v2;
namespace daf = ecu::dut_profiles::daf_sac;
namespace dp = ecu::dut_profile;
namespace runtime = ecu::core::v2::runtime;
namespace transport = ecu::core::v2::transport;
namespace uds = ecu::core::v2::protocol::uds;

constexpr core::time::MonotonicClockDomainId kDomain{77U};

int require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

class TestClock final : public core::time::IMonotonicClock {
 public:
  [[nodiscard]] core::time::MonotonicClockProperties
  properties() const noexcept override {
    return {
        kDomain,
        std::chrono::nanoseconds{1},
        std::chrono::microseconds{10},
        std::chrono::microseconds{1},
        true};
  }

  [[nodiscard]] core::time::MonotonicClockReading
  read() const noexcept override {
    return {
        core::time::MonotonicClockStatus::ok,
        kDomain,
        now_,
        std::chrono::nanoseconds{1}};
  }

  void advance(const core::time::MonotonicDuration delta) noexcept {
    now_ += delta;
  }

 private:
  core::time::MonotonicTime now_{0};
};

class ScriptedDiagnosticTransport final
    : public transport::IDiagnosticTransport {
 public:
  ScriptedDiagnosticTransport() noexcept = default;

  [[nodiscard]] bool valid() const noexcept override {
    return true;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus start_send(
      const std::byte* const payload,
      const std::size_t length) noexcept override {
    if (payload == nullptr || length != 3U || tx_busy_ || received_ready_) {
      return transport::DiagnosticTransportStatus::invalid_argument;
    }

    for (std::size_t index = 0U; index < length; ++index) {
      request_[index] = payload[index];
    }
    request_length_ = length;
    tx_busy_ = true;
    last_tx_status_ = transport::DiagnosticTransportStatus::in_progress;
    return transport::DiagnosticTransportStatus::in_progress;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus service(
      const core::time::MonotonicClockReading& now) noexcept override {
    if (!tx_busy_) {
      return transport::DiagnosticTransportStatus::idle;
    }

    tx_busy_ = false;
    last_tx_status_ = transport::DiagnosticTransportStatus::ok;
    tx_completion_ = now;
    build_response(now);
    return transport::DiagnosticTransportStatus::ok;
  }

  [[nodiscard]] bool tx_busy() const noexcept override {
    return tx_busy_;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus
  last_tx_status() const noexcept override {
    return last_tx_status_;
  }

  [[nodiscard]] core::time::MonotonicClockReading
  tx_completion_timestamp() const noexcept override {
    return tx_completion_;
  }

  [[nodiscard]] bool has_received() const noexcept override {
    return received_ready_;
  }

  [[nodiscard]] std::size_t received_size() const noexcept override {
    return received_ready_ ? response_length_ : 0U;
  }

  [[nodiscard]] transport::DiagnosticTransportStatus take_received(
      std::byte* const destination,
      const std::size_t capacity,
      std::size_t& length,
      core::time::MonotonicClockReading& completion_timestamp) noexcept override {
    length = 0U;
    completion_timestamp = {};
    if (!received_ready_ || destination == nullptr ||
        capacity < response_length_) {
      return transport::DiagnosticTransportStatus::invalid_argument;
    }

    for (std::size_t index = 0U; index < response_length_; ++index) {
      destination[index] = response_[index];
    }
    length = response_length_;
    completion_timestamp = response_completion_;
    received_ready_ = false;
    response_length_ = 0U;
    return transport::DiagnosticTransportStatus::ok;
  }

  void reset() noexcept override {
    tx_busy_ = false;
    received_ready_ = false;
    request_length_ = 0U;
    response_length_ = 0U;
    last_tx_status_ = transport::DiagnosticTransportStatus::idle;
    tx_completion_ = {};
    response_completion_ = {};
  }

  void reject_did(
      const std::uint16_t did,
      const std::uint8_t nrc) noexcept {
    rejected_did_ = did;
    rejected_nrc_ = nrc;
  }

 private:
  static std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
  }

  void build_positive(
      const std::uint16_t did,
      const std::string_view value,
      const core::time::MonotonicClockReading& now) noexcept {
    response_ = {};
    response_[0U] = std::byte{0x62U};
    response_[1U] = static_cast<std::byte>(
        static_cast<std::uint8_t>(did >> 8U));
    response_[2U] = static_cast<std::byte>(
        static_cast<std::uint8_t>(did & 0xFFU));
    response_length_ = 3U + value.size();
    for (std::size_t index = 0U; index < value.size(); ++index) {
      response_[3U + index] =
          static_cast<std::byte>(
              static_cast<std::uint8_t>(value[index]));
    }
    response_completion_ = now;
    received_ready_ = true;
  }

  void build_response(
      const core::time::MonotonicClockReading& now) noexcept {
    if (request_length_ != 3U ||
        byte_value(request_[0U]) != 0x22U) {
      return;
    }

    const auto did = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(
             byte_value(request_[1U]))
         << 8U) |
        byte_value(request_[2U]));

    if (did == rejected_did_) {
      response_ = {};
      response_[0U] = std::byte{0x7FU};
      response_[1U] = std::byte{0x22U};
      response_[2U] = static_cast<std::byte>(rejected_nrc_);
      response_length_ = 3U;
      response_completion_ = now;
      received_ready_ = true;
      return;
    }

    if (did == daf::kDidVin) {
      build_positive(did, "XLRSACPROOF000001", now);
    } else if (did == daf::kDidSoftware) {
      build_positive(did, "SAC-SW-PROOF", now);
    } else if (did == daf::kDidHardware) {
      build_positive(did, "SAC-HW-PROOF", now);
    }
  }

  std::array<std::byte, 8U> request_{};
  std::size_t request_length_{0U};
  std::array<std::byte, 128U> response_{};
  std::size_t response_length_{0U};
  bool tx_busy_{false};
  bool received_ready_{false};
  transport::DiagnosticTransportStatus last_tx_status_{
      transport::DiagnosticTransportStatus::idle};
  core::time::MonotonicClockReading tx_completion_{};
  core::time::MonotonicClockReading response_completion_{};
  std::uint16_t rejected_did_{0U};
  std::uint8_t rejected_nrc_{0U};
};

[[nodiscard]] uds::UdsClientConfig uds_config() noexcept {
  uds::UdsClientConfig result{};
  result.timing.p2 = std::chrono::milliseconds{100};
  result.timing.p2_star = std::chrono::seconds{5};
  result.timestamp_domain = kDomain;
  result.max_timestamp_uncertainty = std::chrono::microseconds{10};
  return result;
}

[[nodiscard]] bench::BenchComponentExecutionContract execution_contract()
    noexcept {
  const auto bound = std::chrono::milliseconds{1};
  return {bound, bound, bound, bound, bound};
}

struct Fixture {
  dp::DutProfileDefinition profile{};
  runtime::DutRegistry duts{};
  runtime::DutHandle dut{};
  dp::ResolvedDutSessionPlan plan{};
};

[[nodiscard]] Fixture resolved_fixture(
    const daf::CanBitrateProfile bitrate) {
  Fixture fixture{};
  fixture.profile = daf::make_profile_definition(bitrate);

  const auto registration = fixture.duts.register_dut(fixture.profile.dut);
  fixture.dut = registration.handle;
  if (registration.status != runtime::DutRegistrationStatus::registered ||
      !fixture.duts.freeze_configuration()) {
    return fixture;
  }

  dp::DutProfileBinding binding{};
  binding.session_owner = 900U;
  binding.dut = fixture.dut;
  binding.timestamp_domain = kDomain;
  binding.resources[0U] = {
      daf::kPrimaryCanRole,
      {runtime::ResourceClass::can_channel, 7U}};
  binding.resource_count = 1U;

  if (dp::resolve_profile_session(
          fixture.profile,
          fixture.duts,
          binding,
          fixture.plan) != dp::ProfileResolveStatus::resolved) {
    fixture.plan = {};
  }
  return fixture;
}

}  // namespace

int main() {
  int failures = 0;

  const auto profile_250 =
      daf::make_profile_definition(daf::CanBitrateProfile::k250k);
  const auto profile_500 =
      daf::make_profile_definition(daf::CanBitrateProfile::k500k);

  failures += require(
      dp::validate_profile_definition(profile_250) ==
              dp::ProfileValidationStatus::valid &&
          dp::validate_profile_definition(profile_500) ==
              dp::ProfileValidationStatus::valid,
      "both evidence-backed SAC bitrate profiles validate");

  failures += require(
      profile_250.dut.profile_id != profile_500.dut.profile_id &&
          profile_250.can_links[0U].nominal_bitrate == 250000U &&
          profile_500.can_links[0U].nominal_bitrate == 500000U,
      "SAC bitrate variants remain explicit stable profiles");

  failures += require(
      profile_250.dut.dut_class == core::domain::DutClass::ecu &&
          profile_250.dut.domains == core::domain::kTruck &&
          dp::has_protocol_requirement(
              profile_250.required_protocols,
              dp::ProtocolRequirement::isotp) &&
          dp::has_protocol_requirement(
              profile_250.required_protocols,
              dp::ProtocolRequirement::uds),
      "SAC is an ECU-class truck UDS/ISO-TP proof");

  failures += require(
      profile_250.rx_expectation_count == 1U &&
          profile_250.rx_expectations[0U].identifier ==
              daf::kResponseCanId &&
          profile_250.rx_expectations[0U].mask == 0x1FFFFFFFU &&
          !profile_250.rx_expectations[0U].match_standard &&
          profile_250.rx_expectations[0U].match_extended,
      "SAC exact 29-bit diagnostic response is profile-owned");

  {
    dp::DutProfileRegistry registry;
    failures += require(
        registry.register_profile(profile_250) ==
                dp::ProfileRegistrationStatus::registered &&
            registry.register_profile(profile_500) ==
                dp::ProfileRegistrationStatus::registered &&
            registry.freeze_configuration() &&
            registry.select(profile_250.dut.profile_id).profile != nullptr &&
            registry.select(profile_500.dut.profile_id).profile != nullptr,
        "both SAC bitrate variants register and select deterministically");
  }

  auto fixture =
      resolved_fixture(daf::CanBitrateProfile::k500k);
  failures += require(
      fixture.plan.profile_id == daf::profile_id(
          daf::CanBitrateProfile::k500k) &&
          fixture.plan.can_links[0U].channel_config.nominal_bitrate ==
              500000U &&
          fixture.plan.can_links[0U].resource ==
              runtime::ResourceKey{
                  runtime::ResourceClass::can_channel,
                  7U},
      "SAC profile resolves onto an arbitrary bench CAN resource");

  {
    TestClock clock;
    ScriptedDiagnosticTransport diagnostic_transport{};
    uds::UdsClient client{diagnostic_transport, uds_config()};
    daf::IdentificationProgram program{
        daf::CanBitrateProfile::k500k,
        client,
        clock,
        execution_contract()};
    dp::DutProfileSessionEndpoint endpoint{fixture.plan, program};

    failures += require(endpoint.valid(), "SAC runtime endpoint validates");
    failures += require(
        endpoint.prepare() == bench::BenchComponentStatus::ok &&
            endpoint.activate() == bench::BenchComponentStatus::ok,
        "SAC profile prepares and activates");

    for (std::size_t iteration = 0U;
         iteration < 12U &&
         program.status() != daf::IdentificationProgramStatus::complete;
         ++iteration) {
      const auto result = endpoint.service();
      failures += require(
          result == bench::BenchComponentStatus::ok ||
              result == bench::BenchComponentStatus::no_action,
          "SAC identification service remains healthy");
      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        program.status() == daf::IdentificationProgramStatus::complete,
        "SAC identification reaches complete state");
    failures += require(
        program.result().vin.view() == "XLRSACPROOF000001" &&
            program.result().software.view() == "SAC-SW-PROOF" &&
            program.result().hardware.view() == "SAC-HW-PROOF",
        "SAC VIN/software/hardware values survive Core V2 UDS path");

    failures += require(
        endpoint.safe_stop() == bench::BenchComponentStatus::ok &&
            endpoint.stop() == bench::BenchComponentStatus::ok,
        "SAC read-only profile follows common safe-stop cleanup");
  }

  {
    TestClock clock;
    ScriptedDiagnosticTransport diagnostic_transport{};
    diagnostic_transport.reject_did(daf::kDidSoftware, 0x31U);
    uds::UdsClient client{diagnostic_transport, uds_config()};
    daf::IdentificationProgram program{
        daf::CanBitrateProfile::k500k,
        client,
        clock,
        execution_contract()};
    dp::DutProfileSessionEndpoint endpoint{fixture.plan, program};

    failures += require(
        endpoint.prepare() == bench::BenchComponentStatus::ok &&
            endpoint.activate() == bench::BenchComponentStatus::ok,
        "negative-response fixture starts");

    bool faulted = false;
    for (std::size_t iteration = 0U;
         iteration < 10U && !faulted;
         ++iteration) {
      faulted =
          endpoint.service() == bench::BenchComponentStatus::fault;
      clock.advance(std::chrono::milliseconds{1});
    }

    failures += require(
        faulted &&
            program.status() == daf::IdentificationProgramStatus::fault &&
            program.last_nrc() == 0x31U &&
            endpoint.snapshot().state == dp::DutProfileEndpointState::faulted,
        "SAC UDS negative response is preserved and fails closed");
    failures += require(
        endpoint.safe_stop() == bench::BenchComponentStatus::ok &&
            endpoint.stop() == bench::BenchComponentStatus::ok,
        "faulted SAC proof still follows common cleanup path");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "DAF_SAC_DUT_PROFILE_STAGE3_3=PASS\n";
  return 0;
}
