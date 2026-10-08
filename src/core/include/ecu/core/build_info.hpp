#pragma once

#include <string_view>

namespace ecu::core {

[[nodiscard]] std::string_view product_name() noexcept;
[[nodiscard]] std::string_view architecture_contract() noexcept;

}  // namespace ecu::core
