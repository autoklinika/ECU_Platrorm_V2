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

[[nodiscard]] bool make_plan(
    runtime::DutRegistry& duts,
    dp::DutProfileRegistry& profiles,
    const core::time::MonotonicClockDomainId domain,
    const transport::CanPhysicalChannelId channel,
    dp::ResolvedDutSessionPlan& destination) noexcept {
  if (!channel.valid() ||
      channel.value > (std::numeric_limits<runtime::ResourceInstance>::max)()) {
    return false;
  }

  const auto definition =
      daf::make_profile_definition(daf::CanBitrateProfile::k250k);
  if (profiles.register_profile(definition) !=
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
  binding.session_owner = 0x53414334U;
  binding.dut = registered.handle;
  binding.timestamp_domain = domain;
  binding.resources[0U] = {
      daf::kPrimaryCanRole,
      {runtime::ResourceClass::can_channel,
       static_cast<runtime::ResourceInstance>(channel.value)}};
  binding.resource_count = 1U;
  return dp::resolve_profile_session(
             *selected.profile, duts, binding, destination) ==
         dp::ProfileResolveStatus::resolved;
}

[[nodiscard]] transport::CanDriverExecutionContract
driver_execution() noexcept {
  return {
      std::chrono::milliseconds{1},
      std::chrono::milliseconds{1},
      std::chrono::milliseconds{100},
      std::chrono::milliseconds{20},
      std::chrono::milliseconds{5},
      std::chrono::milliseconds{5},
      std::chrono::milliseconds{1}};
}

[[nodiscard]] bench::BenchComponentExecutionContract
program_execution() noexcept {
  const auto bound = std::chrono::milliseconds{2};
  return {bound, bound, bound, bound, bound};
}

[[nodiscard]] isotp::IsoTpConfig isotp_config(
    const core::time::MonotonicClockProperties clock) noexcept {
  isotp::IsoTpConfig config{};
  config.frame_format = transport::CanFrameFormat::classic;
  config.tx_data_length = 8U;
  config.bit_rate_switch = false;
  config.rx_block_size = 0U;
  config.rx_stmin = 0U;
  config.max_wait_frames = 3U;
  config.flow_control_timeout = std::chrono::milliseconds{1000};
  config.consecutive_frame_timeout = std::chrono::milliseconds{1000};
  config.timestamp_domain = clock.domain;
  config.max_timestamp_uncertainty = clock.max_uncertainty;
  return config;
}

[[nodiscard]] uds::UdsClientConfig uds_config(
    const core::time::MonotonicClockProperties clock) noexcept {
  uds::UdsClientConfig config{};
  config.timing.p2 = std::chrono::milliseconds{100};
  config.timing.p2_star = std::chrono::milliseconds{5000};
  config.timestamp_domain = clock.domain;
  config.max_timestamp_uncertainty = clock.max_uncertainty;
  return config;
}

[[nodiscard]] int failure(
    const char* reason,
    const app::Application* application = nullptr) {
  std::cerr << "SAC_BENCH_APP_PHYSICAL=FAIL reason=" << reason;
  if (application != nullptr) {
    const auto s = application->snapshot();
    std::cerr << " app=" << static_cast<unsigned int>(s.status)
              << " bench=" << static_cast<unsigned int>(s.bench.state)
              << " can=" << static_cast<unsigned int>(s.can_status)
              << " uds=" << static_cast<unsigned int>(s.uds_status)
              << " transport=" << static_cast<unsigned int>(s.transport_failure)
              << " nrc=" << static_cast<unsigned int>(s.nrc);
  }
  std::cerr << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || std::string_view{argv[2]} != "250000") {
    std::cerr << "usage: ecu_daf_sac_bench_app_probe "
                 "<can-interface> 250000\n";
    return 2;
  }

  const std::string_view iface{argv[1]};
  if (iface.empty() || iface.size() >= 16U) {
    return failure("invalid-interface");
  }

  const auto link = platform::query_socketcan_link(iface.data(), 50);
  if (link.status != platform::SocketCanLinkQueryStatus::ok ||
      !link.info.up || link.info.bus_off ||
      link.info.nominal_bitrate != 250000U ||
      link.info.fd_enabled || link.info.listen_only_enabled) {
    return failure("CAN-link-not-ready-250k-classic-normal");
  }

  platform::BoottimeClock clock{
      std::chrono::milliseconds{1},
      std::chrono::microseconds{100}};
  if (!core::time::is_valid_clock_properties(clock.properties())) {
    return failure("clock-invalid");
  }

  platform::SocketCanAdapter driver{
      iface.data(), clock, driver_execution()};
  if (!driver.valid()) {
    return failure("socketcan-adapter-invalid");
  }

  runtime::DutRegistry duts;
  dp::DutProfileRegistry profiles;
  dp::ResolvedDutSessionPlan plan{};
  if (!make_plan(duts, profiles, clock.properties().domain,
                 driver.physical_channel_id(), plan)) {
    return failure("profile-binding-invalid");
  }

  runtime::ResourceManager resources;
  transport::CanBusRuntime bus{driver};
  isotp::IsoTpAddress address{};
  address.tx_id = daf::kRequestCanId;
  address.rx_id = daf::kResponseCanId;
  address.identifier_format =
      transport::CanIdentifierFormat::extended_29_bit;

  isotp::IsoTpEndpoint iso_endpoint{address, isotp_config(clock.properties())};
  if (!iso_endpoint.valid()) {
    return failure("isotp-endpoint-invalid");
  }

  isotp::IsoTpCanFrameSinkAdapter sink{iso_endpoint};
  transport::CanFilter rx_filter{};
  rx_filter.identifier = plan.rx_expectations[0U].identifier;
  rx_filter.mask = plan.rx_expectations[0U].mask;
  rx_filter.match_standard = plan.rx_expectations[0U].match_standard;
  rx_filter.match_extended = plan.rx_expectations[0U].match_extended;

  const auto subscription = bus.subscribe(
      rx_filter, sink, {std::chrono::milliseconds{1}});
  if (subscription.status != transport::CanSubscriptionStatus::subscribed ||
      !bus.freeze_configuration()) {
    return failure("bus-subscription-invalid");
  }

  isotp::IsoTpDiagnosticTransport diagnostic{iso_endpoint, bus};
  uds::UdsClient uds_client{diagnostic, uds_config(clock.properties())};
  if (!uds_client.valid()) {
    return failure("uds-client-invalid");
  }

  daf::IdentificationProgram program{
      daf::CanBitrateProfile::k250k, uds_client, clock, program_execution()};
  dp::DutProfileSessionEndpoint profile_endpoint{plan, program};
  app::BenchEndpoint bench_endpoint{
      plan, bus, profile_endpoint, program, driver.execution_contract()};
  bench::BenchSession session{
      resources, duts, bench_endpoint, nullptr, nullptr};
  bench::BenchSessionHostRuntime host{session, clock};
  app::Application application{
      plan, bench_endpoint, session, host, program, clock};

  if (!application.configure({std::chrono::milliseconds{250}})) {
    return failure("bench-application-not-configured", &application);
  }

  std::cout << "SAC_BENCH_APP=START profile=0x" << std::hex
            << plan.profile_id << std::dec
            << " bitrate=250000 tx=0x" << std::hex
            << address.tx_id << " rx=0x" << address.rx_id
            << std::dec << " service=read-only-0x22\n";

  if (application.identify() != app::AppStatus::ok) {
    return failure("bench-identification-start", &application);
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{12};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto result = application.service();
    const auto state = application.snapshot().state;
    if (state == app::AppState::identified &&
        result == app::AppStatus::ok) {
      const auto& id = application.identification();
      const auto snapshot = application.snapshot();
      std::cout << "SAC_BENCH_VIN_LAST4="
                << snapshot.vin_suffix.data() << '\n';
      std::cout << "SAC_BENCH_SOFTWARE=" << id.software.view() << '\n';
      std::cout << "SAC_BENCH_HARDWARE=" << id.hardware.view() << '\n';
      std::cout << "SAC_BENCH_RESOURCE_LEASES="
                << resources.active_count() << '\n';
      if (resources.active_count() != 0U ||
          bus.state() == transport::CanBusState::running) {
        return failure("resource-teardown-incomplete", &application);
      }
      std::cout << "SAC_BENCH_APP_PHYSICAL=PASS CORE_V2_BENCH_DUT_PROFILE\n";
      return 0;
    }
    if (state == app::AppState::faulted ||
        (result != app::AppStatus::ok &&
         result != app::AppStatus::no_action)) {
      return failure("bench-identification-failed", &application);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  (void)application.stop();
  return failure("external-12s-time-budget-expired", &application);
}
