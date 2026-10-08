#include "ecu/applications/daf_sac/application.hpp"
#include "daf_sac_clear_operator.hpp"
#include "ecu/core_v2/protocol/isotp/isotp_diagnostic_transport.hpp"
#include "ecu/core_v2/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core_v2/protocol/uds/uds_client.hpp"
#include "ecu/dut_profile/registry.hpp"
#include "ecu/platform/linux/v2/boottime_clock.hpp"
#include "ecu/platform/linux/v2/socketcan_adapter.hpp"
#include "ecu/platform/linux/v2/socketcan_link_info.hpp"

#include <chrono>
#include <iomanip>
#include <poll.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

namespace {
namespace app = ecu::applications::daf_sac;
namespace bench = ecu::bench;
namespace core = ecu::core::v2;
namespace daf = ecu::dut_profiles::daf_sac;
namespace dp = ecu::dut_profile;
namespace isotp = ecu::core::v2::protocol::isotp;
namespace platform = ecu::platform::linux::v2;
namespace runtime = ecu::core::v2::runtime;
namespace transport = ecu::core::v2::transport;
namespace uds = ecu::core::v2::protocol::uds;

[[nodiscard]] bool resolve(
    runtime::DutRegistry& duts,
    dp::DutProfileRegistry& profiles,
    const core::time::MonotonicClockDomainId domain,
    const transport::CanPhysicalChannelId channel,
    dp::ResolvedDutSessionPlan& plan) noexcept {
  if (!channel.valid() ||
      channel.value > (std::numeric_limits<runtime::ResourceInstance>::max)()) {
    return false;
  }
  const auto profile =
      daf::make_profile_definition(daf::CanBitrateProfile::k250k);
  if (profiles.register_profile(profile) !=
          dp::ProfileRegistrationStatus::registered ||
      !profiles.freeze_configuration()) {
    return false;
  }
  const auto selected = profiles.select(
      daf::profile_id(daf::CanBitrateProfile::k250k));
  if (selected.status != dp::ProfileSelectionStatus::selected ||
      selected.profile == nullptr) {
    return false;
  }
  const auto registered = duts.register_dut(selected.profile->dut);
  if (registered.status != runtime::DutRegistrationStatus::registered ||
      !duts.freeze_configuration()) {
    return false;
  }
  dp::DutProfileBinding binding{};
  binding.session_owner = 0x53414342U;
  binding.dut = registered.handle;
  binding.timestamp_domain = domain;
  binding.resources[0U] = {
      daf::kPrimaryCanRole,
      {runtime::ResourceClass::can_channel,
       static_cast<runtime::ResourceInstance>(channel.value)}};
  binding.resource_count = 1U;
  return dp::resolve_profile_session(
             *selected.profile, duts, binding, plan) ==
         dp::ProfileResolveStatus::resolved;
}

[[nodiscard]] transport::CanDriverExecutionContract driver_contract()
    noexcept {
  return {std::chrono::milliseconds{1}, std::chrono::milliseconds{1},
          std::chrono::milliseconds{100}, std::chrono::milliseconds{20},
          std::chrono::milliseconds{5}, std::chrono::milliseconds{5},
          std::chrono::milliseconds{1}};
}
[[nodiscard]] bench::BenchComponentExecutionContract program_contract()
    noexcept {
  const auto duration = std::chrono::milliseconds{2};
  return {duration, duration, duration, duration, duration};
}
[[nodiscard]] isotp::IsoTpConfig iso_config(
    const core::time::MonotonicClockProperties properties) noexcept {
  isotp::IsoTpConfig c{};
  c.frame_format = transport::CanFrameFormat::classic;
  c.tx_data_length = 8U;
  c.bit_rate_switch = false;
  c.rx_block_size = 0U;
  c.rx_stmin = 0U;
  c.max_wait_frames = 3U;
  c.flow_control_timeout = std::chrono::milliseconds{1000};
  c.consecutive_frame_timeout = std::chrono::milliseconds{1000};
  c.timestamp_domain = properties.domain;
  c.max_timestamp_uncertainty = properties.max_uncertainty;
  return c;
}
[[nodiscard]] uds::UdsClientConfig uds_config(
    const core::time::MonotonicClockProperties properties) noexcept {
  uds::UdsClientConfig c{};
  c.timing.p2 = std::chrono::milliseconds{100};
  c.timing.p2_star = std::chrono::milliseconds{5000};
  c.timestamp_domain = properties.domain;
  c.max_timestamp_uncertainty = properties.max_uncertainty;
  return c;
}

[[nodiscard]] int failed(
    const char* reason, const app::Application* application = nullptr) {
  // This binary is the destructive operator CLI, not the read-only gate.
  std::cerr << "SAC_DTC_CLEAR_CLI=FAIL reason=" << reason;
  if (application != nullptr) {
    const auto s = application->snapshot();
    std::cerr << " app_status=" << static_cast<unsigned int>(s.status)
              << " bench=" << static_cast<unsigned int>(s.bench.state)
              << " can=" << static_cast<unsigned int>(s.can_status)
              << " uds=" << static_cast<unsigned int>(s.uds_status)
              << " transport=" << static_cast<unsigned int>(s.transport_failure)
              << " nrc=0x" << std::hex << static_cast<unsigned int>(s.nrc)
              << std::dec;
  }
  std::cerr << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 || std::string_view{argv[2]} != "clear-dtc") {
    std::cerr << "usage: ecu_daf_sac_stage42_clear_probe <ifname> "
                 "clear-dtc <private-evidence-directory>\n";
    return 2;
  }
  if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO)) {
    std::cerr << "ERROR: DTC clearing needs an interactive operator TTY; "
                 "piped/non-interactive commands are disabled\n";
    return 2;
  }
  if (argv[3][0] != '/') {
    return failed("evidence-directory-must-be-absolute");
  }
  const std::string_view iface{argv[1]};
  if (iface.empty() || iface.size() >= 16U) {
    return failed("invalid-interface");
  }

  const auto link = platform::query_socketcan_link(iface.data(), 50);
  if (link.status != platform::SocketCanLinkQueryStatus::ok ||
      !link.info.up || link.info.bus_off ||
      link.info.nominal_bitrate != 250000U ||
      link.info.fd_enabled || link.info.listen_only_enabled) {
    return failed("CAN-link-not-ready-250k-classic-normal");
  }

  platform::BoottimeClock clock{
      std::chrono::milliseconds{1}, std::chrono::microseconds{100}};
  if (!core::time::is_valid_clock_properties(clock.properties())) {
    return failed("invalid-clock");
  }
  platform::SocketCanAdapter driver{iface.data(), clock, driver_contract()};
  if (!driver.valid()) {
    return failed("invalid-socketcan");
  }

  runtime::DutRegistry duts;
  dp::DutProfileRegistry profiles;
  dp::ResolvedDutSessionPlan plan{};
  if (!resolve(duts, profiles, clock.properties().domain,
               driver.physical_channel_id(), plan)) {
    return failed("DUT-binding");
  }

  runtime::ResourceManager resources;
  transport::CanBusRuntime bus{driver};

  isotp::IsoTpAddress address{};
  address.tx_id = daf::kRequestCanId;
  address.rx_id = daf::kResponseCanId;
  address.identifier_format =
      transport::CanIdentifierFormat::extended_29_bit;
  isotp::IsoTpEndpoint iso_endpoint{address, iso_config(clock.properties())};
  if (!iso_endpoint.valid()) {
    return failed("ISO-TP");
  }
  isotp::IsoTpCanFrameSinkAdapter uds_sink{iso_endpoint};
  daf::PressureMonitor pressure;

  transport::CanFilter diag_filter{};
  diag_filter.identifier = daf::kResponseCanId;
  diag_filter.mask = 0x1FFFFFFFU;
  diag_filter.match_standard = false;
  diag_filter.match_extended = true;

  transport::CanFilter pressure_filter{};
  pressure_filter.identifier = plan.rx_expectations[1U].identifier;
  pressure_filter.mask = plan.rx_expectations[1U].mask;
  pressure_filter.match_standard = false;
  pressure_filter.match_extended = true;

  if (bus.subscribe(
          diag_filter, uds_sink,
          {std::chrono::milliseconds{1}}).status !=
          transport::CanSubscriptionStatus::subscribed ||
      bus.subscribe(
          pressure_filter, pressure,
          {std::chrono::milliseconds{1}}).status !=
          transport::CanSubscriptionStatus::subscribed ||
      !bus.freeze_configuration()) {
    return failed("CAN-subscription");
  }

  isotp::IsoTpDiagnosticTransport diagnostic{iso_endpoint, bus};
  uds::UdsClient client{diagnostic, uds_config(clock.properties())};
  if (!client.valid()) {
    return failed("UDS");
  }
  daf::ServiceProgram services{
      daf::CanBitrateProfile::k250k, client, clock, program_contract()};
  dp::DutProfileSessionEndpoint profile_endpoint{plan, services};
  app::BenchEndpoint bench_endpoint{
      plan, bus, profile_endpoint, services, driver.execution_contract()};
  bench::BenchSession session{
      resources, duts, bench_endpoint, nullptr, nullptr};
  bench::BenchSessionHostRuntime host{session, clock};
  app::Application application{
      plan, bench_endpoint, session, host, services, pressure, clock};

  if (!application.configure({std::chrono::milliseconds{250}})) {
    return failed("Bench-not-configured", &application);
  }

  std::cout << "SAC_DTC_CLEAR_CLI=START profile=0x" << std::hex
            << plan.profile_id << std::dec
            << " bitrate=250000 service=read-before-confirmed-clear\n";

  const auto execute_until = [&](const app::AppState expected) {
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds{12};
    while (std::chrono::steady_clock::now() < until) {
      const auto result = application.service();
      const auto state = application.snapshot().state;
      if (state == expected && result == app::AppStatus::ok) {
        return true;
      }
      if (state == app::AppState::faulted ||
          (result != app::AppStatus::ok &&
           result != app::AppStatus::no_action)) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    (void)application.stop();
    return false;
  };

  // A fresh full-mask read is mandatory in the very same process before the
  // action can be armed. There is no "skip read" option.
  if (application.read_dtcs(0xFFU) != app::AppStatus::ok ||
      !execute_until(app::AppState::dtcs_ready)) {
    return failed("pre-clear-DTC-read-failed", &application);
  }
  if (resources.active_count() != 0U ||
      bus.state() == transport::CanBusState::running) {
    return failed("pre-clear-session-not-clean", &application);
  }

  const auto& before = application.dtcs();
  std::cout << "SAC_PRE_CLEAR_AVAILABILITY_MASK=0x" << std::hex
            << static_cast<unsigned int>(before.status_availability)
            << std::dec << "\nSAC_PRE_CLEAR_DTC_COUNT=" << before.count
            << '\n';
  for (std::size_t i = 0U; i < before.count; ++i) {
    std::cout << "SAC_DTC_BEFORE=0x" << std::uppercase << std::hex
              << std::setw(6) << std::setfill('0')
              << before.records[i].code << " STATUS=0x"
              << std::setw(2)
              << static_cast<unsigned int>(before.records[i].status)
              << std::dec << std::nouppercase << '\n';
  }
  std::cout.flush();

  ecu::tools::sac_clear::EvidenceFile evidence;
  if (!evidence.create(argv[3], before, plan.profile_id)) {
    return failed("pre-clear-evidence-durability-error-NO-ERASE");
  }
  std::cout << "SAC_DTC_BACKUP=" << evidence.path() << '\n';

  if (before.count == 0U) {
    (void)evidence.append("CLEAR_SKIPPED=NO_RECORDED_DTC\n");
    std::cout << "SAC_DTC_CLEAR=SKIPPED (no DTC in full-mask read)\n";
    return 0;
  }

  const auto challenge = application.prepare_clear_dtcs();
  if (challenge.sequence == 0U ||
      challenge.profile_id != plan.profile_id ||
      challenge.inspected_dtc_count != before.count) {
    (void)evidence.append("CLEAR_SKIPPED=INVALID_CHALLENGE\n");
    return failed("could-not-arm-confirmation-NO-ERASE", &application);
  }

  const auto expected =
      ecu::tools::sac_clear::confirmation_phrase(before.count);
  std::cout
      << "\nUWAGA: Kasowanie DTC usuwa zapisana historie diagnostyczna.\n"
      << "SAC ma " << before.count << " odczytanych DTC.\n"
      << "Zapis oryginalnych DTC istnieje na dysku: " << evidence.path()
      << "\nNie wykonuj kasowania, jesli nie chcesz utracic tych informacji.\n"
      << "Aby POTWIERDZIC SKASOWANIE wpisz DOKLADNIE: " << expected
      << "\nInny tekst lub brak odpowiedzi w 120 s anuluje bez kasowania.\n"
      << "> " << std::flush;

  struct pollfd descriptor {};
  descriptor.fd = STDIN_FILENO;
  descriptor.events = POLLIN;
  const int polled = ::poll(&descriptor, 1, 120000);
  if (polled <= 0 || (descriptor.revents & POLLIN) == 0) {
    (void)evidence.append("OPERATOR_CONFIRMATION=TIMEOUT_OR_INPUT_ERROR\n");
    std::cout << "\nSAC_DTC_CLEAR=CANCELLED_NO_TX\n";
    return 3;
  }

  std::string typed;
  if (!std::getline(std::cin, typed) ||
      !ecu::tools::sac_clear::confirmation_matches(typed, before.count)) {
    (void)evidence.append("OPERATOR_CONFIRMATION=REJECTED\n");
    std::cout << "SAC_DTC_CLEAR=CANCELLED_NO_TX\n";
    return 3;
  }

  // This durable audit line is written BEFORE the potentially destructive
  // command, otherwise abort without sending UDS service 0x14.
  if (!evidence.append(
          "OPERATOR_CONFIRMATION=EXPLICIT_ONE_TIME\n"
          "CLEAR_INTENT=UDS_10_03_THEN_14_FF_FF_FF\n"
          "CLEAR_ATTEMPT=UNRESOLVED_UNTIL_VERIFIED\n")) {
    return failed("pre-clear-confirmation-audit-not-durable-NO-ERASE");
  }

  std::cout << "SAC_DTC_CLEAR_REQUEST=START (10 03 -> 14 FF FF FF)\n"
            << std::flush;
  if (application.clear_dtcs(challenge, true) != app::AppStatus::ok) {
    (void)evidence.append("CLEAR_START=REJECTED\n");
    return failed("clear-not-started", &application);
  }
  if (!execute_until(app::AppState::dtcs_clear_acknowledged)) {
    (void)evidence.append("CLEAR_OUTCOME=UNKNOWN (NRC/TIMEOUT/ERROR)\n");
    std::cerr << "SAC_DTC_CLEAR_OUTCOME=UNKNOWN; DO NOT AUTO-RETRY\n";
    return failed("clear-unconfirmed", &application);
  }
  if (!application.snapshot().clear_acknowledged ||
      resources.active_count() != 0U ||
      bus.state() == transport::CanBusState::running) {
    (void)evidence.append("CLEAR_OUTCOME=UNKNOWN (CLEANUP/ACK ERROR)\n");
    return failed("clear-ACK-or-cleanup-invalid", &application);
  }
  if (!evidence.append("CLEAR_UDS_54_ACK=YES\n")) {
    std::cerr << "WARNING: DTC clear ACK but audit append failed\n";
    return failed("clear-ACK-audit-write-error", &application);
  }
  std::cout << "SAC_DTC_CLEAR_UDS_ACK=YES\n"
            << "SAC_DTC_CLEAR_VERIFICATION=RE-READ_DTC\n";

  // An ACK does NOT prove that the faults disappeared. The legacy SAC
  // application finished on 0x54 and started a NEW read only when the
  // operator refreshed the view. In V2 the immediate post-clear 10 03
  // sometimes times out, despite a 50 03 visible in parallel CAN capture.
  // Allow the SAC to settle before a new standalone read-only Bench session.
  // Never re-send 0x14 under any recovery condition.
  constexpr auto kPostClearSettle = std::chrono::milliseconds{1000};
  std::cout << "SAC_POST_CLEAR_SETTLE_MS=" << kPostClearSettle.count() << '\n'
            << std::flush;
  std::this_thread::sleep_for(kPostClearSettle);

  const auto verify_read = [&]() {
    return application.read_dtcs(0xFFU) == app::AppStatus::ok &&
           execute_until(app::AppState::dtcs_ready);
  };
  bool verified = verify_read();
  if (!verified) {
    const auto first = application.snapshot();
    std::ostringstream diagnostic;
    diagnostic << "POST_CLEAR_FIRST_READ=FAILED app_status="
               << static_cast<unsigned int>(first.status)
               << " uds=" << static_cast<unsigned int>(first.uds_status)
               << " nrc=" << static_cast<unsigned int>(first.nrc)
               << " transport="
               << static_cast<unsigned int>(first.transport_failure) << '\n';
    if (!evidence.append(diagnostic.str())) {
      return failed("post-clear-first-read-evidence-not-durable", &application);
    }
    std::cerr << "SAC_POST_CLEAR_FIRST_READ=FAIL; UDS 54 ACK RETAINED\n";
    if (first.state != app::AppState::faulted ||
        resources.active_count() != 0U ||
        bus.state() == transport::CanBusState::running ||
        application.recover() != app::AppStatus::ok) {
      (void)evidence.append("POST_CLEAR_VERIFICATION=RECOVERY_BLOCKED\n");
      return failed("post-clear-read-only-recovery-blocked", &application);
    }
    // Exactly one retry of 10 03 / 19 02 FF only, in a fresh session.
    // The previously acknowledged destructive request remains one-shot.
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});
    std::cout << "SAC_POST_CLEAR_READ_ONLY_RETRY=ONE\n" << std::flush;
    verified = verify_read();
    if (verified && !evidence.append(
                        "POST_CLEAR_READ_ONLY_RETRY=RECOVERED\n")) {
      return failed("post-clear-read-recovery-evidence-not-durable");
    }
  }
  if (!verified) {
    (void)evidence.append("POST_CLEAR_VERIFICATION=FAILED\n");
    std::cerr << "SAC_POST_CLEAR_READ=FAIL; CLEAR WAS ACKNOWLEDGED\n";
    return failed("post-clear-DTC-read-failed", &application);
  }
  if (resources.active_count() != 0U ||
      bus.state() == transport::CanBusState::running) {
    (void)evidence.append("POST_CLEAR_VERIFICATION=CLEANUP_FAILED\n");
    return failed("post-clear-session-not-clean", &application);
  }

  const auto& after = application.dtcs();
  std::cout << "SAC_DTC_AFTER_COUNT=" << after.count << '\n';
  std::ostringstream after_log;
  after_log << "POST_CLEAR_DTC_COUNT=" << after.count << '\n';
  for (std::size_t i = 0U; i < after.count; ++i) {
    std::cout << "SAC_DTC_AFTER=0x" << std::uppercase << std::hex
              << std::setw(6) << std::setfill('0')
              << after.records[i].code << " STATUS=0x"
              << std::setw(2)
              << static_cast<unsigned int>(after.records[i].status)
              << std::dec << std::nouppercase << '\n';
    after_log << "POST_CLEAR_DTC=0x" << std::uppercase << std::hex
              << std::setw(6) << std::setfill('0')
              << after.records[i].code << " STATUS=0x"
              << std::setw(2)
              << static_cast<unsigned int>(after.records[i].status)
              << std::dec << '\n';
  }
  after_log << "POST_CLEAR_VERIFICATION=READ_COMPLETED\n";
  if (!evidence.append(after_log.str())) {
    return failed("post-clear-evidence-not-durable", &application);
  }

  std::cout << "SAC_DTC_CLEAR_PHYSICAL=ACKNOWLEDGED_AND_RECHECKED\n"
            << "SAC_BENCH_RESOURCE_LEASES=" << resources.active_count()
            << "\nSAC_DTC_BACKUP=" << evidence.path() << '\n';
  if (after.count != 0U) {
    std::cout << "SAC_DTC_CLEAR_NOTE=REMAINING_OR_REAPPEARED_FAULTS\n";
  }
  return 0;
}
