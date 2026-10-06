#pragma once

#include "ecu/core/runtime/command.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ecu::core::runtime {

struct CommandView {
  CommandHeader header{};
  const std::byte* payload{nullptr};
  std::size_t payload_size{0U};
};

enum class CommandPolicyDecision : std::uint8_t {
  allow,
  deny,
  confirmation_required,
};

class ICommandPolicy {
 public:
  virtual ~ICommandPolicy() = default;

  [[nodiscard]] virtual CommandPolicyDecision evaluate(
      const CommandView& command) const noexcept = 0;
};

class AllowAllCommandPolicy final : public ICommandPolicy {
 public:
  [[nodiscard]] CommandPolicyDecision evaluate(
      const CommandView&) const noexcept override {
    return CommandPolicyDecision::allow;
  }
};

class ICommandHandler {
 public:
  virtual ~ICommandHandler() = default;
  [[nodiscard]] virtual CommandResult handle(
      const CommandView& command) noexcept = 0;
};

enum class CommandRegistrationStatus : std::uint8_t {
  registered,
  already_registered,
  type_conflict,
  invalid_argument,
  capacity_exhausted,
};

enum class CommandDispatchStatus : std::uint8_t {
  dispatched,
  invalid_command,
  no_handler,
  policy_denied,
  confirmation_required,
};

struct CommandDispatchResult {
  CommandDispatchStatus status{CommandDispatchStatus::invalid_command};
  CommandResult result{};
};

class CommandDispatcher {
 public:
  static constexpr std::size_t kMaxHandlers = 128U;

  explicit CommandDispatcher(
      const ICommandPolicy& policy) noexcept;

  [[nodiscard]] CommandRegistrationStatus register_handler(
      CommandTypeId type,
      ICommandHandler& handler) noexcept;

  [[nodiscard]] bool unregister_handler(
      CommandTypeId type,
      ICommandHandler& handler) noexcept;

  [[nodiscard]] CommandDispatchResult dispatch(
      const CommandView& command) noexcept;

  [[nodiscard]] std::size_t handler_count() const noexcept;

 private:
  struct Entry {
    CommandTypeId type{0U};
    ICommandHandler* handler{nullptr};
  };

  [[nodiscard]] Entry* find(CommandTypeId type) noexcept;
  [[nodiscard]] const Entry* find(CommandTypeId type) const noexcept;
  [[nodiscard]] Entry* find_free() noexcept;

  const ICommandPolicy& policy_;
  mutable std::mutex mutex_{};
  std::array<Entry, kMaxHandlers> entries_{};
};

}  // namespace ecu::core::runtime
