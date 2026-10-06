#include "ecu/core/runtime/command_dispatcher.hpp"

namespace ecu::core::runtime {

CommandDispatcher::CommandDispatcher(
    const ICommandPolicy& policy) noexcept
    : policy_(policy) {}

CommandRegistrationStatus CommandDispatcher::register_handler(
    const CommandTypeId type,
    ICommandHandler& handler) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  if (type == 0U) {
    return CommandRegistrationStatus::invalid_argument;
  }

  if (auto* existing = find(type)) {
    if (existing->handler == &handler) {
      return CommandRegistrationStatus::already_registered;
    }

    return CommandRegistrationStatus::type_conflict;
  }

  auto* entry = find_free();
  if (entry == nullptr) {
    return CommandRegistrationStatus::capacity_exhausted;
  }

  entry->type = type;
  entry->handler = &handler;
  return CommandRegistrationStatus::registered;
}

bool CommandDispatcher::unregister_handler(
    const CommandTypeId type,
    ICommandHandler& handler) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  auto* entry = find(type);
  if (entry == nullptr || entry->handler != &handler) {
    return false;
  }

  *entry = Entry{};
  return true;
}

CommandDispatchResult CommandDispatcher::dispatch(
    const CommandView& command) noexcept {
  if (!is_valid_command_header(command.header) ||
      (command.payload_size != 0U && command.payload == nullptr)) {
    return CommandDispatchResult{
        CommandDispatchStatus::invalid_command,
        CommandResult{
            CommandOutcome::rejected,
            0U}};
  }

  const auto policy_decision = policy_.evaluate(command);
  if (policy_decision == CommandPolicyDecision::deny) {
    return CommandDispatchResult{
        CommandDispatchStatus::policy_denied,
        CommandResult{
            CommandOutcome::rejected,
            0U}};
  }

  if (policy_decision ==
      CommandPolicyDecision::confirmation_required) {
    return CommandDispatchResult{
        CommandDispatchStatus::confirmation_required,
        CommandResult{
            CommandOutcome::rejected,
            0U}};
  }

  ICommandHandler* handler = nullptr;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto* entry = find(command.header.type);
    if (entry != nullptr) {
      handler = entry->handler;
    }
  }

  if (handler == nullptr) {
    return CommandDispatchResult{
        CommandDispatchStatus::no_handler,
        CommandResult{
            CommandOutcome::rejected,
            0U}};
  }

  return CommandDispatchResult{
      CommandDispatchStatus::dispatched,
      handler->handle(command)};
}

std::size_t CommandDispatcher::handler_count() const noexcept {
  std::lock_guard<std::mutex> lock{mutex_};

  std::size_t count = 0U;
  for (const auto& entry : entries_) {
    if (entry.handler != nullptr) {
      ++count;
    }
  }
  return count;
}

CommandDispatcher::Entry* CommandDispatcher::find(
    const CommandTypeId type) noexcept {
  for (auto& entry : entries_) {
    if (entry.handler != nullptr && entry.type == type) {
      return &entry;
    }
  }
  return nullptr;
}

const CommandDispatcher::Entry* CommandDispatcher::find(
    const CommandTypeId type) const noexcept {
  for (const auto& entry : entries_) {
    if (entry.handler != nullptr && entry.type == type) {
      return &entry;
    }
  }
  return nullptr;
}

CommandDispatcher::Entry* CommandDispatcher::find_free() noexcept {
  for (auto& entry : entries_) {
    if (entry.handler == nullptr) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace ecu::core::runtime
