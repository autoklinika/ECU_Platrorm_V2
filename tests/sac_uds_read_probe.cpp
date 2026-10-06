#include "ecu/core/protocol/isotp/isotp_endpoint.hpp"
#include "ecu/core/protocol/uds/uds_client.hpp"
#include "ecu/core/protocol/uds/uds_services.hpp"
#include "ecu/platform/linux/socketcan/socketcan_adapter.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

namespace {

class SteadyClock final : public ecu::core::time::IMonotonicClock {
 public:
  ecu::core::time::MonotonicTime now() const noexcept override {
    return std::chrono::duration_cast<ecu::core::time::MonotonicTime>(
        std::chrono::steady_clock::now().time_since_epoch());
  }
};

std::uint32_t parse_u32(const char* value) {
  return static_cast<std::uint32_t>(
      std::stoul(value, nullptr, 0));
}

void print_payload(const ecu::core::protocol::uds::UdsResponse& response) {
  std::cout << "response_hex=";
  for (std::size_t i = 0U; i < response.length; ++i) {
    if (i != 0U) {
      std::cout << ' ';
    }
    std::cout
        << std::hex
        << std::uppercase
        << std::setw(2)
        << std::setfill('0')
        << static_cast<unsigned int>(
               std::to_integer<std::uint8_t>(
                   response.payload[i]));
  }
  std::cout << std::dec << '\n';

  if (response.status ==
          ecu::core::protocol::uds::UdsStatus::ok &&
      response.length >= 3U &&
      std::to_integer<std::uint8_t>(
          response.payload[0]) == 0x62U) {
    std::cout << "data_ascii=";
    for (std::size_t i = 3U; i < response.length; ++i) {
      const auto value =
          std::to_integer<std::uint8_t>(
              response.payload[i]);
      std::cout
          << ((value >= 0x20U && value <= 0x7EU)
                  ? static_cast<char>(value)
                  : '.');
    }
    std::cout << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr
        << "usage: ecu_sac_uds_read_probe "
        << "<ifname> <bitrate> <tx-id> <rx-id> <did>\n";
    return 2;
  }

  const std::string interface_name{argv[1]};
  const auto bitrate = parse_u32(argv[2]);
  const auto tx_id = parse_u32(argv[3]);
  const auto rx_id = parse_u32(argv[4]);
  const auto did =
      static_cast<std::uint16_t>(parse_u32(argv[5]));

  SteadyClock clock;

  ecu::platform::linux::socketcan::SocketCanAdapter can{
      interface_name,
      clock};

  ecu::core::transport::CanChannelConfig can_config{};
  can_config.nominal_bitrate = bitrate;
  can_config.fd_enabled = false;
  can_config.data_bitrate = 0U;
  can_config.mode = ecu::core::transport::CanMode::normal;

  const auto open_status = can.open(can_config);
  if (open_status != ecu::core::transport::CanStatus::ok) {
    std::cerr
        << "SAC_CAN_OPEN=FAIL status="
        << static_cast<unsigned int>(open_status)
        << '\n';
    return 1;
  }

  ecu::core::protocol::isotp::IsoTpAddress address{};
  address.tx_id = tx_id;
  address.rx_id = rx_id;
  address.identifier_format =
      ecu::core::transport::CanIdentifierFormat::extended_29_bit;

  ecu::core::protocol::isotp::IsoTpConfig isotp_config{};
  isotp_config.frame_format =
      ecu::core::transport::CanFrameFormat::classic;
  isotp_config.tx_data_length = 8U;
  isotp_config.bit_rate_switch = false;
  isotp_config.rx_block_size = 0U;
  isotp_config.rx_stmin = 0U;
  isotp_config.flow_control_timeout =
      std::chrono::milliseconds{1000};
  isotp_config.consecutive_frame_timeout =
      std::chrono::milliseconds{1000};

  ecu::core::protocol::isotp::IsoTpEndpoint transport{
      can,
      clock,
      address,
      isotp_config};

  ecu::core::protocol::uds::UdsClient uds{
      transport,
      clock,
      ecu::core::protocol::uds::UdsTiming{
          std::chrono::milliseconds{100},
          std::chrono::milliseconds{5000}}};

  if (!transport.valid() || !uds.valid()) {
    std::cerr << "SAC_UDS_STACK=FAIL invalid-config\n";
    can.close();
    return 1;
  }

  const auto request =
      ecu::core::protocol::uds::make_read_data_by_identifier(did);

  std::cout
      << "SAC_UDS_REQUEST sid=0x22 did=0x"
      << std::hex
      << std::uppercase
      << std::setw(4)
      << std::setfill('0')
      << static_cast<unsigned int>(did)
      << " tx_id=0x"
      << tx_id
      << " rx_id=0x"
      << rx_id
      << std::dec
      << " bitrate="
      << bitrate
      << '\n';

  const auto start = uds.start_request(request);
  if (start != ecu::core::protocol::uds::UdsStatus::in_progress) {
    std::cerr
        << "SAC_UDS_START=FAIL status="
        << static_cast<unsigned int>(start)
        << '\n';
    can.close();
    return 1;
  }

  const auto hard_deadline =
      std::chrono::steady_clock::now() +
      std::chrono::seconds{7};

  while (
      std::chrono::steady_clock::now() <
          hard_deadline &&
      !uds.has_response()) {
    const auto status = uds.poll();

    if (status != ecu::core::protocol::uds::UdsStatus::ok &&
        status != ecu::core::protocol::uds::UdsStatus::idle &&
        status != ecu::core::protocol::uds::UdsStatus::in_progress) {
      break;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds{1});
  }

  if (!uds.has_response()) {
    std::cerr << "SAC_UDS_PROBE=FAIL no-response-object\n";
    can.close();
    return 1;
  }

  const auto response = uds.take_response();
  print_payload(response);

  if (response.status ==
      ecu::core::protocol::uds::UdsStatus::ok) {
    std::cout << "SAC_UDS_PROBE=PASS POSITIVE\n";
    can.close();
    return 0;
  }

  if (response.status ==
      ecu::core::protocol::uds::UdsStatus::negative_response) {
    std::cout
        << "SAC_UDS_PROBE=PASS NEGATIVE nrc=0x"
        << std::hex
        << std::uppercase
        << std::setw(2)
        << std::setfill('0')
        << static_cast<unsigned int>(
               response.negative_response_code)
        << std::dec
        << '\n';
    can.close();
    return 0;
  }

  std::cerr
      << "SAC_UDS_PROBE=FAIL status="
      << static_cast<unsigned int>(response.status)
      << '\n';
  can.close();
  return 1;
}
