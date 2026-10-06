#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ecu::core::storage {

enum class StorageStatus : std::uint8_t {
  ok,
  not_found,
  invalid_argument,
  buffer_too_small,
  read_only,
  io_error,
};

class IKeyValueStore {
 public:
  virtual ~IKeyValueStore() = default;

  virtual StorageStatus read(
      std::string_view key,
      std::byte* destination,
      std::size_t capacity,
      std::size_t& length) noexcept = 0;

  virtual StorageStatus write(
      std::string_view key,
      const std::byte* data,
      std::size_t length) noexcept = 0;

  virtual StorageStatus erase(
      std::string_view key) noexcept = 0;
};

}  // namespace ecu::core::storage
