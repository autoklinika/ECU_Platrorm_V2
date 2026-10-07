#include "ecu/applications/daf_sac/application.hpp"
#include "ecu/core_v2/protocol/isotp/isotp_diagnostic_transport.hpp"
#include "ecu/core_v2/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core_v2/protocol/uds/uds_client.hpp"
#include "ecu/dut_profile/registry.hpp"
#include "ecu/platform/linux/v2/boottime_clock.hpp"
#include "ecu/platform/linux/v2/socketcan_adapter.hpp"
#include "ecu/platform/linux/v2/socketcan_link_info.hpp"

#include <chrono>
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
  std::cerr << "SAC_STAGE42_READ_PHYSICAL=FAIL reason=" << reason;
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
  if (argc != 3 ||
      (std::string_view{argv[2]} != "parameters" &&
       std::string_view{argv[2]} != "dtc")) {
    std::cerr << "usage: ecu_daf_sac_stage42_read_probe <ifname> "
                 "<parameters|dtc> (NO DTC CLEAR)\n";
    return 2;
  }
  const std::string_view mode{argv[2]};
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

  std::cout << "SAC_STAGE42_READ=START mode=" << mode
            << " bitrate=250000 profile=0x" << std::hex << plan.profile_id
            << std::dec
            << " no-clear-no-write-no-output-control\n";

  const auto start = mode == "parameters"
      ? application.read_parameters() : application.read_dtcs(0xFFU);
  if (start != app::AppStatus::ok) {
    return failed("start", &application);
  }

  const auto finish = mode == "parameters"
      ? app::AppState::parameters_ready : app::AppState::dtcs_ready;
  const auto until =
      std::chrono::steady_clock::now() + std::chrono::seconds{12};
  while (std::chrono::steady_clock::now() < until) {
    const auto status = application.service();
    const auto state = application.snapshot().state;
    if (state == finish && status == app::AppStatus::ok) {
      if (mode == "parameters") {
        const auto voltage = application.voltage();
        const auto pressures = application.pressure();
        std::cout << "SAC_FE96_PERMANENT_V=" << voltage.permanent_v << '\n'
                  << "SAC_FE96_IGNITION_V=" << voltage.ignition_v << '\n'
                  << "SAC_PGN_FEAE_OBSERVED=" << pressures.received << '\n';
        if (pressures.pressure1_valid) {
          std::cout << "SAC_PRESSURE1_BAR=" << pressures.pressure1_bar << '\n';
        } else {
          std::cout << "SAC_PRESSURE1_BAR=UNAVAILABLE\n";
        }
        if (pressures.pressure2_valid) {
          std::cout << "SAC_PRESSURE2_BAR=" << pressures.pressure2_bar << '\n';
        } else {
          std::cout << "SAC_PRESSURE2_BAR=UNAVAILABLE\n";
        }
      } else {
        const auto& result = application.dtcs();
        std::cout << "SAC_DTC_AVAILABILITY_MASK=0x" << std::hex
                  << static_cast<unsigned int>(result.status_availability)
                  << std::dec << '\n';
        std::cout << "SAC_DTC_COUNT=" << result.count << '\n';
        for (std::size_t i = 0U; i < result.count; ++i) {
          std::cout << "SAC_DTC_CODE=0x" << std::hex
                    << result.records[i].code
                    << " STATUS=0x"
                    << static_cast<unsigned int>(result.records[i].status)
                    << std::dec << '\n';
        }
      }

      std::cout << "SAC_BENCH_RESOURCE_LEASES="
                << resources.active_count() << '\n';
      if (resources.active_count() != 0U ||
          bus.state() == transport::CanBusState::running) {
        return failed("cleanup-incomplete", &application);
      }
      std::cout << "SAC_STAGE42_READ_PHYSICAL=PASS\n";
      return 0;
    }
    if (state == app::AppState::faulted ||
        (status != app::AppStatus::ok &&
         status != app::AppStatus::no_action)) {
      return failed("Bench-session-fault", &application);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  (void)application.stop();
  return failed("12s-overall-time-budget", &application);
}
