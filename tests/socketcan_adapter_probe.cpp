#include "ecu/platform/linux/socketcan/socketcan_adapter.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

class SteadyClock final : public ecu::core::time::IMonotonicClock {
 public:
  ecu::core::time::MonotonicTime now() const noexcept override {
    return std::chrono::duration_cast<ecu::core::time::MonotonicTime>(
        std::chrono::steady_clock::now().time_since_epoch());
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr
        << "usage: ecu_socketcan_adapter_probe "
        << "<ifname> <nominal-bitrate> <data-bitrate>\n";
    return 2;
  }

  const auto nominal =
      static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
  const auto data =
      static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));

  SteadyClock clock;
  ecu::platform::linux::socketcan::SocketCanAdapter adapter{
      argv[1], clock};

  ecu::core::transport::CanChannelConfig config{};
  config.nominal_bitrate = nominal;
  config.fd_enabled = true;
  config.data_bitrate = data;
  config.mode = ecu::core::transport::CanMode::listen_only;

  const auto open_status = adapter.open(config);
  if (open_status != ecu::core::transport::CanStatus::ok) {
    std::cerr << "SOCKETCAN_ADAPTER_OPEN=FAIL status="
              << static_cast<int>(open_status) << '\n';
    return 1;
  }

  if (!adapter.is_open()) {
    std::cerr << "SOCKETCAN_ADAPTER_OPEN=FAIL not-open-after-success\n";
    return 1;
  }

  ecu::core::transport::CanFrame blocked_tx{};
  blocked_tx.identifier = 0x123U;
  const auto tx_status = adapter.send(blocked_tx);
  if (tx_status != ecu::core::transport::CanStatus::unsupported) {
    std::cerr << "LISTEN_ONLY_TX_GUARD=FAIL status="
              << static_cast<int>(tx_status) << '\n';
    return 1;
  }

  std::cout << "LISTEN_ONLY_TX_GUARD=PASS\n";

  const auto receive = adapter.try_receive();
  if (receive.status != ecu::core::transport::CanStatus::would_block &&
      receive.status != ecu::core::transport::CanStatus::ok) {
    std::cerr << "SOCKETCAN_RECEIVE_PROBE=FAIL status="
              << static_cast<int>(receive.status) << '\n';
    return 1;
  }

  std::cout << "SOCKETCAN_RECEIVE_PROBE=PASS status="
            << static_cast<int>(receive.status) << '\n';

  adapter.close();
  std::cout << "SOCKETCAN_ADAPTER_PROBE=PASS\n";
  return 0;
}
