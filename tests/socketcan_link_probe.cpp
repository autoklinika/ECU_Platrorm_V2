#include "ecu/platform/linux/socketcan/socketcan_link_info.hpp"

#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ecu_socketcan_link_probe <ifname>\n";
    return 2;
  }

  const auto result =
      ecu::platform::linux::socketcan::query_socketcan_link(argv[1]);

  if (result.status !=
      ecu::platform::linux::socketcan::LinkQueryStatus::ok) {
    std::cerr << "SOCKETCAN_LINK_PROBE=FAIL status="
              << static_cast<int>(result.status) << '\n';
    return 1;
  }

  const auto& info = result.info;
  std::cout
      << "interface=" << argv[1]
      << " up=" << info.up
      << " bus_off=" << info.bus_off
      << " nominal_bitrate=" << info.nominal_bitrate
      << " data_bitrate=" << info.data_bitrate
      << " fd_enabled=" << info.fd_enabled
      << " listen_only_enabled=" << info.listen_only_enabled
      << " cap_classic=" << info.capabilities.classic_can
      << " cap_fd=" << info.capabilities.can_fd
      << " cap_brs=" << info.capabilities.bit_rate_switch
      << " cap_listen_only=" << info.capabilities.listen_only
      << " max_payload="
      << static_cast<unsigned int>(info.capabilities.max_payload_bytes)
      << '\n';

  std::cout << "SOCKETCAN_LINK_PROBE=PASS\n";
  return 0;
}
