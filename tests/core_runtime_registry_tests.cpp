#include "ecu/core/actuation/actuator.hpp"
#include "ecu/core/device/device_registry.hpp"
#include "ecu/core/network/datagram_channel.hpp"
#include "ecu/core/network/stream_channel.hpp"
#include "ecu/core/runtime/command_dispatcher.hpp"
#include "ecu/core/runtime/core_facade.hpp"
#include "ecu/core/runtime/module_registry.hpp"
#include "ecu/core/runtime/state_registry.hpp"
#include "ecu/core/safety/safety_watchdog.hpp"
#include "ecu/core/security/authorization.hpp"
#include "ecu/core/security/device_identity.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

using namespace ecu::core;

int require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

class FakeClock final : public time::IMonotonicClock {
 public:
  time::MonotonicTime now() const noexcept override {
    return now_;
  }

  void advance(const std::chrono::milliseconds delta) noexcept {
    now_ += std::chrono::duration_cast<time::MonotonicTime>(delta);
  }

 private:
  time::MonotonicTime now_{0};
};

class TestPolicy final : public runtime::ICommandPolicy {
 public:
  runtime::CommandPolicyDecision evaluate(
      const runtime::CommandView& command) const noexcept override {
    if (command.header.type == 2U) {
      return runtime::CommandPolicyDecision::deny;
    }
    if (command.header.type == 3U) {
      return runtime::CommandPolicyDecision::confirmation_required;
    }
    return runtime::CommandPolicyDecision::allow;
  }
};

class TestHandler final : public runtime::ICommandHandler {
 public:
  runtime::CommandResult handle(
      const runtime::CommandView& command) noexcept override {
    ++calls;
    last_id = command.header.id;
    return runtime::CommandResult{
        runtime::CommandOutcome::succeeded,
        0U};
  }

  std::size_t calls{0U};
  runtime::CommandId last_id{0U};
};

class TestStateProvider final : public runtime::IStateProvider {
 public:
  runtime::StateTypeId state_type() const noexcept override {
    return 100U;
  }

  runtime::StateRevision revision() const noexcept override {
    return 7U;
  }

  runtime::StateSnapshotStatus snapshot(
      std::byte* destination,
      const std::size_t capacity,
      std::size_t& length,
      runtime::StateHeader& header) const noexcept override {
    static constexpr std::array<std::byte, 3> data{
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30}};

    length = 0U;
    if (destination == nullptr) {
      return runtime::StateSnapshotStatus::invalid_argument;
    }
    if (capacity < data.size()) {
      return runtime::StateSnapshotStatus::buffer_too_small;
    }

    std::memcpy(destination, data.data(), data.size());
    length = data.size();
    header.revision = revision();
    header.updated_at = time::MonotonicTime{123};
    return runtime::StateSnapshotStatus::ok;
  }
};

class TestModule final : public runtime::ICoreModule {
 public:
  explicit TestModule(const runtime::ModuleId id) : id_(id) {}

  runtime::ModuleDescriptor descriptor() const noexcept override {
    return runtime::ModuleDescriptor{
        id_,
        runtime::ModuleKind::ecu_module,
        0x01U};
  }

  runtime::LifecycleState state() const noexcept override {
    return lifecycle_.state();
  }

  runtime::LifecycleStatus start() noexcept override {
    const auto begin = lifecycle_.begin_start();
    if (begin != runtime::LifecycleStatus::ok) {
      return begin;
    }
    return lifecycle_.mark_running();
  }

  runtime::LifecycleStatus poll() noexcept override {
    return runtime::LifecycleStatus::ok;
  }

  runtime::LifecycleStatus stop() noexcept override {
    const auto begin = lifecycle_.begin_stop();
    if (begin != runtime::LifecycleStatus::ok) {
      return begin;
    }
    return lifecycle_.mark_stopped();
  }

 private:
  runtime::ModuleId id_;
  runtime::LifecycleStateMachine lifecycle_{};
};

class TestDevice final : public device::IDevice {
 public:
  explicit TestDevice(const device::DeviceId id) : id_(id) {}

  device::DeviceDescriptor descriptor() const noexcept override {
    return device::DeviceDescriptor{
        id_,
        device::DeviceClass::can_interface,
        0x05U};
  }

  bool available() const noexcept override {
    return true;
  }

 private:
  device::DeviceId id_;
};

class TestActuator final : public actuation::IActuator {
 public:
  actuation::ActuatorDescriptor descriptor() const noexcept override {
    return actuation::ActuatorDescriptor{1U, 0x01U};
  }

  actuation::ActuatorState actuator_state() const noexcept override {
    return actuator_state_;
  }

  actuation::ActuatorStatus execute(
      const runtime::CommandView&) noexcept override {
    if (actuator_state_ != actuation::ActuatorState::ready) {
      return actuation::ActuatorStatus::not_ready;
    }
    actuator_state_ = actuation::ActuatorState::active;
    return actuation::ActuatorStatus::ok;
  }

  actuation::ActuatorStatus safe_stop() noexcept override {
    actuator_state_ = actuation::ActuatorState::ready;
    return actuation::ActuatorStatus::ok;
  }

  runtime::LifecycleState state() const noexcept override {
    return lifecycle_.state();
  }

  runtime::LifecycleStatus start() noexcept override {
    const auto begin = lifecycle_.begin_start();
    if (begin != runtime::LifecycleStatus::ok) {
      return begin;
    }
    const auto running = lifecycle_.mark_running();
    if (running == runtime::LifecycleStatus::ok) {
      actuator_state_ = actuation::ActuatorState::ready;
    }
    return running;
  }

  runtime::LifecycleStatus poll() noexcept override {
    return runtime::LifecycleStatus::ok;
  }

  runtime::LifecycleStatus stop() noexcept override {
    static_cast<void>(safe_stop());
    const auto begin = lifecycle_.begin_stop();
    if (begin != runtime::LifecycleStatus::ok) {
      return begin;
    }
    const auto stopped = lifecycle_.mark_stopped();
    if (stopped == runtime::LifecycleStatus::ok) {
      actuator_state_ = actuation::ActuatorState::disabled;
    }
    return stopped;
  }

 private:
  runtime::LifecycleStateMachine lifecycle_{};
  actuation::ActuatorState actuator_state_{
      actuation::ActuatorState::disabled};
};

}  // namespace

int main() {
  int failures = 0;

  {
    TestPolicy policy;
    runtime::CommandDispatcher dispatcher{policy};
    TestHandler handler;

    failures += require(
        dispatcher.register_handler(1U, handler) ==
            runtime::CommandRegistrationStatus::registered,
        "register command handler");
    failures += require(
        dispatcher.register_handler(1U, handler) ==
            runtime::CommandRegistrationStatus::already_registered,
        "duplicate handler idempotent");

    runtime::CommandView command{};
    command.header.id = 11U;
    command.header.type = 1U;

    const auto dispatched = dispatcher.dispatch(command);
    failures += require(
        dispatched.status == runtime::CommandDispatchStatus::dispatched &&
            dispatched.result.outcome ==
                runtime::CommandOutcome::succeeded &&
            handler.calls == 1U &&
            handler.last_id == 11U,
        "allowed command dispatched");

    command.header.type = 2U;
    failures += require(
        dispatcher.dispatch(command).status ==
            runtime::CommandDispatchStatus::policy_denied,
        "policy denial");

    command.header.type = 3U;
    failures += require(
        dispatcher.dispatch(command).status ==
            runtime::CommandDispatchStatus::confirmation_required,
        "confirmation policy");

    command.header.type = 4U;
    failures += require(
        dispatcher.dispatch(command).status ==
            runtime::CommandDispatchStatus::no_handler,
        "unknown command rejected");

    failures += require(
        dispatcher.unregister_handler(1U, handler) &&
            dispatcher.handler_count() == 0U,
        "unregister command handler");
  }

  {
    runtime::StateRegistry states;
    TestStateProvider provider;

    failures += require(
        states.register_provider(provider) ==
            runtime::StateRegistrationStatus::registered,
        "register state provider");
    failures += require(
        states.find(100U) == &provider,
        "find state provider");

    TestPolicy policy;
    runtime::CommandDispatcher commands{policy};
    runtime::CoreFacade facade{commands, states};

    std::array<std::byte, 8> buffer{};
    std::size_t length = 0U;
    runtime::StateHeader header{};

    failures += require(
        facade.snapshot(
            100U,
            buffer.data(),
            buffer.size(),
            length,
            header) == runtime::StateSnapshotStatus::ok &&
            length == 3U &&
            header.revision == 7U &&
            buffer[0] == std::byte{0x10},
        "CoreFacade state snapshot");

    failures += require(
        facade.snapshot(
            999U,
            buffer.data(),
            buffer.size(),
            length,
            header) == runtime::StateSnapshotStatus::unavailable,
        "unknown state unavailable");

    failures += require(
        states.unregister_provider(provider) &&
            states.provider_count() == 0U,
        "unregister state provider");
  }

  {
    runtime::ModuleRegistry modules;
    TestModule module{10U};
    TestModule conflict{10U};

    failures += require(
        modules.register_module(module) ==
            runtime::ModuleRegistrationStatus::registered,
        "register module");
    failures += require(
        modules.register_module(conflict) ==
            runtime::ModuleRegistrationStatus::id_conflict,
        "module id conflict");
    failures += require(
        modules.find(10U) == &module,
        "find module");
    failures += require(
        module.start() == runtime::LifecycleStatus::ok &&
            module.state() == runtime::LifecycleState::running,
        "module lifecycle start");
    failures += require(
        module.stop() == runtime::LifecycleStatus::ok &&
            module.state() == runtime::LifecycleState::stopped,
        "module lifecycle stop");
  }

  {
    device::DeviceRegistry devices;
    TestDevice first{1U};
    TestDevice conflict{1U};

    failures += require(
        devices.register_device(first) ==
            device::DeviceRegistrationStatus::registered,
        "register device");
    failures += require(
        devices.register_device(conflict) ==
            device::DeviceRegistrationStatus::id_conflict,
        "device id conflict");
    failures += require(
        devices.find(1U) == &first &&
            devices.find(1U)->available(),
        "device lookup and availability");
  }

  {
    FakeClock clock;
    safety::SafetyWatchdog watchdog{clock};

    failures += require(
        watchdog.arm(std::chrono::milliseconds{10}) ==
            safety::WatchdogStatus::ok &&
            watchdog.state() == safety::WatchdogState::armed,
        "watchdog arm");

    clock.advance(std::chrono::milliseconds{5});
    failures += require(
        watchdog.poll() == safety::WatchdogStatus::ok,
        "watchdog before deadline");

    failures += require(
        watchdog.kick() == safety::WatchdogStatus::ok,
        "watchdog kick");

    clock.advance(std::chrono::milliseconds{9});
    failures += require(
        watchdog.poll() == safety::WatchdogStatus::ok,
        "watchdog kick extends deadline");

    clock.advance(std::chrono::milliseconds{1});
    failures += require(
        watchdog.poll() == safety::WatchdogStatus::expired &&
            watchdog.state() == safety::WatchdogState::expired,
        "watchdog expires");
  }

  {
    TestActuator actuator;
    failures += require(
        actuator.start() == runtime::LifecycleStatus::ok &&
            actuator.actuator_state() ==
                actuation::ActuatorState::ready,
        "actuator starts ready");

    runtime::CommandView command{};
    command.header.id = 1U;
    command.header.type = 1U;

    failures += require(
        actuator.execute(command) ==
            actuation::ActuatorStatus::ok &&
            actuator.actuator_state() ==
                actuation::ActuatorState::active,
        "actuator execute");

    failures += require(
        actuator.safe_stop() ==
            actuation::ActuatorStatus::ok &&
            actuator.actuator_state() ==
                actuation::ActuatorState::ready,
        "actuator safe stop");

    failures += require(
        actuator.stop() == runtime::LifecycleStatus::ok &&
            actuator.actuator_state() ==
                actuation::ActuatorState::disabled,
        "actuator stop");
  }

  if (failures != 0) {
    return 1;
  }

  std::cout << "CORE_RUNTIME_REGISTRY_TESTS=PASS\n";
  return 0;
}
