#pragma once

#include "ecu/core/runtime/command_dispatcher.hpp"
#include "ecu/core/runtime/state_registry.hpp"

#include <cstddef>

namespace ecu::core::runtime {

class CoreFacade {
 public:
  CoreFacade(
      CommandDispatcher& commands,
      StateRegistry& states) noexcept;

  [[nodiscard]] CommandDispatchResult submit(
      const CommandView& command) noexcept;

  [[nodiscard]] StateSnapshotStatus snapshot(
      StateTypeId type,
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length,
      StateHeader& header) const noexcept;

 private:
  CommandDispatcher& commands_;
  StateRegistry& states_;
};

}  // namespace ecu::core::runtime
