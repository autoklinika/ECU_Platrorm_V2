#include "ecu/platform/linux/socketcan/detail/socketcan_codec.hpp"

#include <linux/can.h>

#include <cstddef>
#include <iostream>

namespace {

using namespace ecu::core::transport;
using namespace ecu::platform::linux::socketcan::detail;

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int failures = 0;

  CanFrame classic{};
  classic.identifier = 0x321U;
  classic.length = 3U;
  classic.payload[0] = std::byte{0x11};
  classic.payload[1] = std::byte{0x22};
  classic.payload[2] = std::byte{0x33};

  can_frame kernel_classic{};
  failures += require(
      encode_classic_frame(classic, kernel_classic) == CanStatus::ok,
      "encode classic frame");
  failures += require(
      kernel_classic.can_id == 0x321U,
      "classic identifier preserved");
  failures += require(kernel_classic.len == 3U, "classic length preserved");
  failures += require(
      kernel_classic.data[1] == 0x22U,
      "classic payload preserved");

  CanFrame extended = classic;
  extended.identifier_format = CanIdentifierFormat::extended_29_bit;
  extended.identifier = 0x1ABCDEU;

  failures += require(
      encode_classic_frame(extended, kernel_classic) == CanStatus::ok,
      "encode extended classic frame");
  failures += require(
      (kernel_classic.can_id & CAN_EFF_FLAG) != 0U,
      "EFF flag set");

  CanReceiveResult decoded_classic{};
  failures += require(
      decode_classic_frame(
          kernel_classic,
          ecu::core::time::MonotonicTime{123},
          decoded_classic) == CanStatus::ok,
      "decode classic frame");
  failures += require(
      decoded_classic.value.frame.identifier == extended.identifier,
      "extended identifier decoded");
  failures += require(
      decoded_classic.value.timestamp ==
          ecu::core::time::MonotonicTime{123},
      "classic timestamp preserved");

  CanFrame fd{};
  fd.identifier = 0x456U;
  fd.format = CanFrameFormat::fd;
  fd.length = 12U;
  fd.bit_rate_switch = true;
  fd.error_state_indicator = true;
  fd.payload[11] = std::byte{0xAB};

  canfd_frame kernel_fd{};
  failures += require(
      encode_fd_frame(fd, kernel_fd) == CanStatus::ok,
      "encode FD frame");
  failures += require(kernel_fd.len == 12U, "FD length preserved");
  failures += require(
      (kernel_fd.flags & CANFD_BRS) != 0U,
      "BRS flag encoded");
  failures += require(
      (kernel_fd.flags & CANFD_ESI) != 0U,
      "ESI flag encoded");
  failures += require(
      kernel_fd.data[11] == 0xABU,
      "FD payload preserved");

  CanReceiveResult decoded_fd{};
  failures += require(
      decode_fd_frame(
          kernel_fd,
          ecu::core::time::MonotonicTime{456},
          decoded_fd) == CanStatus::ok,
      "decode FD frame");
  failures += require(
      decoded_fd.value.frame.bit_rate_switch,
      "BRS decoded");
  failures += require(
      decoded_fd.value.frame.error_state_indicator,
      "ESI decoded");
  failures += require(
      decoded_fd.value.frame.length == 12U,
      "FD decoded length");

  can_frame error_frame{};
  error_frame.can_id = CAN_ERR_FLAG;
  failures += require(
      decode_classic_frame(
          error_frame,
          ecu::core::time::MonotonicTime{},
          decoded_classic) == CanStatus::invalid_argument,
      "error frame excluded from normal frame decoder");

  if (failures != 0) {
    return 1;
  }

  std::cout << "SOCKETCAN_CODEC_TESTS=PASS\n";
  return 0;
}
