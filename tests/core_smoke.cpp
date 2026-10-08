#include "ecu/core/build_info.hpp"

#include <iostream>
#include <string_view>

int main() {
  constexpr std::string_view expected_product{"ECU Platform V2"};
  constexpr std::string_view expected_contract{"platform-agnostic-core"};

  if (ecu::core::product_name() != expected_product) {
    std::cerr << "Unexpected product identity\n";
    return 1;
  }

  if (ecu::core::architecture_contract() != expected_contract) {
    std::cerr << "Unexpected architecture contract marker\n";
    return 2;
  }

  std::cout << "ECU_CORE_SMOKE=PASS\n";
  return 0;
}
