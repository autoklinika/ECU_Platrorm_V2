#pragma once

#include "ecu/core/transport/i_can_interface.hpp"

#include <array>
#include <cstddef>
#include <mutex>

namespace ecu::core::simulation {

class SimulatedCanInterface final
    : public transport::ICanInterface {
 public:
  static constexpr std::size_t kQueueCapacity = 256U;

  explicit SimulatedCanInterface(
      transport::CanCapabilities capabilities =
          transport::CanCapabilities{
              true, true, true, true, 64U}) noexcept;

  [[nodiscard]] transport::CanCapabilities capabilities()
      const noexcept override;

  transport::CanStatus open(
      const transport::CanChannelConfig& config) noexcept override;

  void close() noexcept override;

  [[nodiscard]] bool is_open() const noexcept override;

  transport::CanStatus send(
      const transport::CanFrame& frame) noexcept override;

  [[nodiscard]] transport::CanReceiveResult try_receive()
      noexcept override;

  [[nodiscard]] bool inject_rx(
      const transport::ReceivedCanFrame& frame) noexcept;

  [[nodiscard]] bool take_tx(
      transport::CanFrame& frame) noexcept;

  [[nodiscard]] std::size_t pending_rx() const noexcept;
  [[nodiscard]] std::size_t pending_tx() const noexcept;

 private:
  template <typename T>
  struct Queue {
    std::array<T, kQueueCapacity> items{};
    std::size_t head{0U};
    std::size_t tail{0U};
    std::size_t count{0U};
  };

  template <typename T>
  static bool push(Queue<T>& queue, const T& value) noexcept {
    if (queue.count >= kQueueCapacity) {
      return false;
    }

    queue.items[queue.tail] = value;
    queue.tail = (queue.tail + 1U) % kQueueCapacity;
    ++queue.count;
    return true;
  }

  template <typename T>
  static bool pop(Queue<T>& queue, T& value) noexcept {
    if (queue.count == 0U) {
      return false;
    }

    value = queue.items[queue.head];
    queue.head = (queue.head + 1U) % kQueueCapacity;
    --queue.count;
    return true;
  }

  mutable std::mutex mutex_{};
  transport::CanCapabilities capabilities_{};
  transport::CanChannelConfig config_{};
  bool open_{false};
  Queue<transport::ReceivedCanFrame> rx_{};
  Queue<transport::CanFrame> tx_{};
};

}  // namespace ecu::core::simulation
