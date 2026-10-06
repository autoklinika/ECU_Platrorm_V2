#pragma once

#include "ecu/core_v2/transport/can_types.hpp"

namespace ecu::core::v2::transport {

class ICanDriver {
 public:
  virtual ~ICanDriver() = default;

  // Lease ownership is independent of the physical open/closed state.
  // A runtime keeps this lease across bus_off/io_error/not_open faults until
  // explicit stop/recover/destruction. This prevents another runtime from
  // acquiring the same physical channel while the previous owner is faulted.
  // owner_token is an opaque identity token and must be non-null.
  [[nodiscard]] virtual bool try_acquire_lease(
      const void* owner_token) noexcept = 0;
  virtual void release_lease(const void* owner_token) noexcept = 0;

  [[nodiscard]] virtual CanCapabilities capabilities() const noexcept = 0;
  // Core invokes open() only from the closed state. For an attempted open
  // starting closed, ok acquires the channel; every non-ok return leaves
  // the driver closed and releases all partial resources.
  // The adapter performs rollback itself; callers must not close failed opens.
  // Access must be serialized, with one runtime acquiring a driver at a time.
  [[nodiscard]] virtual CanStatus open(
      const CanChannelConfig& config) noexcept = 0;

  // Must be idempotent and safe when the driver is already closed.
  // Core V2 calls close() only for a successfully acquired channel.
  virtual void close() noexcept = 0;
  [[nodiscard]] virtual bool is_open() const noexcept = 0;

  // The frame is borrowed only for this call. The adapter must consume or
  // copy everything it needs before returning. ok means accepted into the
  // adapter's bounded TX path; it does not guarantee physical transmission.
  [[nodiscard]] virtual CanStatus try_send(
      const CanFrame& frame) noexcept = 0;
  [[nodiscard]] virtual CanReceiveResult try_receive() noexcept = 0;
};

}  // namespace ecu::core::v2::transport
