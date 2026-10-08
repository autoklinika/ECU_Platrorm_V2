#include "ecu/core_v2/protocol/isotp/isotp_diagnostic_transport.hpp"
#include "ecu/core_v2/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core_v2/protocol/uds/uds_client.hpp"
#include "ecu/core_v2/runtime/dut_registry.hpp"
#include "ecu/core_v2/transport/can_bus_runtime.hpp"
#include "ecu/dut_profile/profile.hpp"
#include "ecu/dut_profile/runtime.hpp"
#include "ecu/dut_profiles/daf_sac/identification_program.hpp"
#include "ecu/dut_profiles/daf_sac/profile.hpp"
#include "ecu/platform/linux/v2/boottime_clock.hpp"
#include "ecu/platform/linux/v2/socketcan_adapter.hpp"
#include "ecu/platform/linux/v2/socketcan_link_info.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

namespace bench = ecu::bench;
namespace core = ecu::core::v2;
namespace daf = ecu::dut_profiles::daf_sac;
namespace dp = ecu::dut_profile;
namespace isotp = ecu::core::v2::protocol::isotp;
namespace platform = ecu::platform::linux::v2;
namespace runtime = ecu::core::v2::runtime;
namespace transport = ecu::core::v2::transport;
namespace uds = ecu::core::v2::protocol::uds;

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

[[nodiscard]] bool link_matches(
    const platform::SocketCanLinkInfo& link,
    const std::uint32_t bitrate) noexcept {
  return link.up &&
         !link.bus_off &&
         link.nominal_bitrate == bitrate &&
         !link.fd_enabled &&
         !link.listen_only_enabled;
}

[[nodiscard]] const char* issue_text(
    const daf::IdentificationReplyIssue issue) noexcept {
  using Issue = daf::IdentificationReplyIssue;
  switch (issue) {
    case Issue::none: return "none";
    case Issue::invalid_positive_header: return "invalid-positive-header";
    case Issue::unexpected_did: return "unexpected-did";
    case Issue::invalid_text_length: return "invalid-text-length";
    case Issue::non_printable_character: return "non-printable-character";
  }
  return "invalid-enum";
}

void print_link(
    const platform::SocketCanLinkQueryResult& query) {
  std::cout
      << "SAC_LINK status="
      << static_cast<unsigned int>(query.status)
      << " up=" << query.info.up
      << " bus_off=" << query.info.bus_off
      << " bitrate=" << query.info.nominal_bitrate
      << " fd=" << query.info.fd_enabled
      << " data_bitrate=" << query.info.data_bitrate
      << " listen_only=" << query.info.listen_only_enabled
      << '\n';
}

[[nodiscard]] bool resolve_plan(
    const daf::CanBitrateProfile bitrate,
    const core::time::MonotonicClockDomainId domain,
    dp::ResolvedDutSessionPlan& plan) noexcept {
  const auto definition =
      daf::make_profile_definition(bitrate);

  runtime::DutRegistry duts;
  const auto registration =
      duts.register_dut(definition.dut);
  if (registration.status !=
          runtime::DutRegistrationStatus::registered ||
      !duts.freeze_configuration()) {
    return false;
  }

  dp::DutProfileBinding binding{};
  binding.session_owner = 0x53414333U;
  binding.dut = registration.handle;
  binding.timestamp_domain = domain;
  binding.resources[0U] = {
      daf::kPrimaryCanRole,
      {runtime::ResourceClass::can_channel, 1U}};
  binding.resource_count = 1U;

  return dp::resolve_profile_session(
             definition,
             duts,
             binding,
             plan) == dp::ProfileResolveStatus::resolved;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr
        << "usage: ecu_daf_sac_core_v2_probe "
        << "<ifname> <250000|500000>\n";
    return 2;
  }

  const std::string_view interface_name{argv[1]};
  const std::string_view bitrate_text{argv[2]};

  daf::CanBitrateProfile bitrate_profile{};
  std::uint32_t bitrate = 0U;
  if (bitrate_text == "250000") {
    bitrate_profile = daf::CanBitrateProfile::k250k;
    bitrate = 250000U;
  } else if (bitrate_text == "500000") {
    bitrate_profile = daf::CanBitrateProfile::k500k;
    bitrate = 500000U;
  } else {
    std::cerr << "SAC_PHYSICAL_PROBE=FAIL invalid-bitrate\n";
    return 2;
  }

  const auto link =
      platform::query_socketcan_link(
          interface_name.data(),
          50);
  print_link(link);

  if (link.status !=
          platform::SocketCanLinkQueryStatus::ok ||
      !link_matches(link.info, bitrate)) {
    std::cerr
        << "SAC_PHYSICAL_PREFLIGHT=LINK_NOT_READY "
        << "required_bitrate=" << bitrate
        << " required_mode=normal\n";
    return 3;
  }

  platform::BoottimeClock clock{
      std::chrono::milliseconds{1},
      std::chrono::microseconds{100}};
  if (!core::time::is_valid_clock_properties(
          clock.properties())) {
    std::cerr << "SAC_CLOCK=FAIL\n";
    return 1;
  }

  platform::SocketCanAdapter driver{
      interface_name.data(),
      clock,
      driver_execution()};
  if (!driver.valid()) {
    std::cerr << "SAC_SOCKETCAN_V2=FAIL invalid-adapter\n";
    return 1;
  }

  dp::ResolvedDutSessionPlan plan{};
  if (!resolve_plan(
          bitrate_profile,
          clock.properties().domain,
          plan)) {
    std::cerr << "SAC_PROFILE_RESOLVE=FAIL\n";
    return 1;
  }

  transport::CanBusRuntime bus{driver};

  isotp::IsoTpAddress address{};
  address.tx_id = daf::kRequestCanId;
  address.rx_id = daf::kResponseCanId;
  address.identifier_format =
      transport::CanIdentifierFormat::extended_29_bit;

  isotp::IsoTpConfig isotp_config{};
  isotp_config.frame_format =
      transport::CanFrameFormat::classic;
  isotp_config.tx_data_length = 8U;
  isotp_config.bit_rate_switch = false;
  isotp_config.rx_block_size = 0U;
  isotp_config.rx_stmin = 0U;
  isotp_config.max_wait_frames = 3U;
  isotp_config.flow_control_timeout =
      std::chrono::milliseconds{1000};
  isotp_config.consecutive_frame_timeout =
      std::chrono::milliseconds{1000};
  isotp_config.timestamp_domain =
      clock.properties().domain;
  isotp_config.max_timestamp_uncertainty =
      clock.properties().max_uncertainty;

  isotp::IsoTpEndpoint endpoint{
      address,
      isotp_config};
  if (!endpoint.valid()) {
    std::cerr << "SAC_ISOTP=FAIL invalid-config\n";
    return 1;
  }

  isotp::IsoTpCanFrameSinkAdapter sink{endpoint};
  transport::CanFilter filter{};
  filter.identifier = daf::kResponseCanId;
  filter.mask = 0x1FFFFFFFU;
  filter.match_standard = false;
  filter.match_extended = true;

  const auto subscription =
      bus.subscribe(
          filter,
          sink,
          {std::chrono::milliseconds{1}});
  if (subscription.status !=
          transport::CanSubscriptionStatus::subscribed ||
      !bus.freeze_configuration()) {
    std::cerr << "SAC_CAN_RUNTIME_CONFIG=FAIL\n";
    return 1;
  }

  const auto start_status =
      bus.start(plan.can_links[0U].channel_config);
  if (start_status != transport::CanStatus::ok) {
    std::cerr
        << "SAC_CAN_RUNTIME_START=FAIL status="
        << static_cast<unsigned int>(start_status)
        << '\n';
    return 1;
  }

  isotp::IsoTpDiagnosticTransport
      diagnostic_transport{endpoint, bus};

  uds::UdsClientConfig uds_config{};
  uds_config.timing.p2 =
      std::chrono::milliseconds{100};
  uds_config.timing.p2_star =
      std::chrono::milliseconds{5000};
  uds_config.timestamp_domain =
      clock.properties().domain;
  uds_config.max_timestamp_uncertainty =
      clock.properties().max_uncertainty;

  uds::UdsClient uds_client{
      diagnostic_transport,
      uds_config};
  if (!uds_client.valid()) {
    std::cerr << "SAC_UDS=FAIL invalid-config\n";
    bus.stop();
    return 1;
  }

  daf::IdentificationProgram program{
      bitrate_profile,
      uds_client,
      clock,
      program_execution()};
  dp::DutProfileSessionEndpoint
      profile_endpoint{plan, program};

  if (!profile_endpoint.valid() ||
      profile_endpoint.prepare() !=
          bench::BenchComponentStatus::ok ||
      profile_endpoint.activate() !=
          bench::BenchComponentStatus::ok) {
    std::cerr << "SAC_PROFILE_ENDPOINT=FAIL\n";
    bus.stop();
    return 1;
  }

  std::cout
      << "SAC_PHYSICAL_PROBE=START "
      << "tx=0x" << std::hex << daf::kRequestCanId
      << " rx=0x" << daf::kResponseCanId
      << std::dec
      << " bitrate=" << bitrate
      << " mode=read-only-identification\n";

  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::seconds{10};

  bool faulted = false;
  while (std::chrono::steady_clock::now() < deadline &&
         program.status() !=
             daf::IdentificationProgramStatus::complete) {
    const auto poll = bus.poll(16U);
    if (poll.status != transport::CanStatus::ok) {
      std::cerr
          << "SAC_CAN_POLL=FAIL status="
          << static_cast<unsigned int>(poll.status)
          << '\n';
      faulted = true;
      break;
    }

    const auto service =
        profile_endpoint.service();
    if (service == bench::BenchComponentStatus::fault) {
      const auto reply = program.last_reply_diagnostic();
      std::cerr << "SAC_IDENT_REPLY_META requested_did=0x" << std::hex
                << reply.requested_did
                << " observed_did=0x" << reply.observed_did
                << std::dec
                << " response_length=" << reply.response_length
                << " issue=" << issue_text(reply.issue)
                << " invalid_octet_offset=" << reply.invalid_octet_offset
                << " invalid_octet_value=0x" << std::hex
                << static_cast<unsigned int>(reply.invalid_octet_value)
                << std::dec << '\n';
      std::cerr
          << "SAC_PROFILE_SERVICE=FAIL uds_status="
          << static_cast<unsigned int>(program.last_uds_status())
          << " transport_failure="
          << static_cast<unsigned int>(program.last_transport_failure())
          << " nrc=0x"
          << std::hex
          << static_cast<unsigned int>(program.last_nrc())
          << std::dec
          << '\n';
      faulted = true;
      break;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds{1});
  }

  const bool complete =
      !faulted &&
      program.status() ==
          daf::IdentificationProgramStatus::complete;

  if (complete) {
    const auto& result = program.result();
    if (result.vin_unprogrammed_ff17) {
      std::cout << "SAC_VIN_STATUS=UNPROGRAMMED_FF17\n"
                << "SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN\n";
    } else {
      std::cout << "SAC_VIN_STATUS=VALID_ASCII\n"
                << "SAC_VIN=" << result.vin.view() << '\n';
    }
    std::cout
        << "SAC_SOFTWARE=" << result.software.view()
        << '\n';
    std::cout
        << "SAC_HARDWARE=" << result.hardware.view()
        << '\n';
  }

  const auto safe_stop =
      profile_endpoint.safe_stop();
  const auto stop =
      profile_endpoint.stop();
  bus.stop();

  if (!complete ||
      safe_stop != bench::BenchComponentStatus::ok ||
      stop != bench::BenchComponentStatus::ok) {
    std::cerr << "SAC_PHYSICAL_PROBE=FAIL\n";
    return 1;
  }

  std::cout
      << "SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN\n";
  return 0;
}
