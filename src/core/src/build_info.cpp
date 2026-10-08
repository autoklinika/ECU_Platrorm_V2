#include "ecu/core/build_info.hpp"

namespace ecu::core {

std::string_view product_name() noexcept {
  return "ECU Platform V2";
}

std::string_view architecture_contract() noexcept {
  return "platform-agnostic-core";
}

}  // namespace ecu::core
