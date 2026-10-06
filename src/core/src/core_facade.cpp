#include "ecu/core/runtime/core_facade.hpp"

namespace ecu::core::runtime {

CoreFacade::CoreFacade(
    CommandDispatcher& commands,
    StateRegistry& states) noexcept
    : commands_(commands),
      states_(states) {}

CommandDispatchResult CoreFacade::submit(
    const CommandView& command) noexcept {
  return commands_.dispatch(command);
}

StateSnapshotStatus CoreFacade::snapshot(
    const StateTypeId type,
    std::byte* destination,
    const std::size_t capacity,
    std::size_t& length,
    StateHeader& header) const noexcept {
  length = 0U;

  const auto* provider = states_.find(type);
  if (provider == nullptr) {
    return StateSnapshotStatus::unavailable;
  }

  return provider->snapshot(
      destination,
      capacity,
      length,
      header);
}

}  // namespace ecu::core::runtime
