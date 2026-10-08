#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ecu::api::v1 {

// Only application-owned observations are publishable. Missing data is not
// interpreted as an inactive DUT, an empty DTC list, or a healthy bus.
enum class ReadStatus : std::uint8_t {
  ok, backend_unavailable, no_active_session, unsupported, invalid_snapshot
};

template <class T>
struct ReadResult {
  ReadStatus status{ReadStatus::backend_unavailable};
  T value{};
};

struct PlatformInfo {
  std::string product;
  std::string version;
};

struct CanInterfaceInfo {
  std::string name;
  bool up{false};
  bool bus_off{false};
  bool fd_enabled{false};
  bool listen_only{false};
  std::uint32_t bitrate{0};
  std::uint32_t data_bitrate{0};
};

struct InterfacesInfo {
  std::vector<CanInterfaceInfo> interfaces;
};

enum class BenchPhase : std::uint8_t {
  unconfigured, ready, starting, running, stopping, recovering, faulted
};

struct BenchInfo {
  BenchPhase phase{BenchPhase::unconfigured};
  std::uint32_t profile_id{0};
  std::uint64_t revision{0};
  bool configured{false};
  bool cleanup_required{false};
};

enum class DutKind : std::uint8_t {
  ecu, actuator, sensor, gateway, network_node, other
};

struct DutInfo {
  std::uint32_t profile_id{0};
  DutKind kind{DutKind::other};
  // Selection never means that physical DUT presence was verified.
  std::string profile_label;
};

enum class ReadCapability : std::uint8_t {
  identification, dtc_read, live_parameters
};

struct CapabilitiesInfo {
  std::vector<ReadCapability> available_read_operations;
};

struct DtcEntry {
  std::string code;
  std::uint8_t status_mask{0};
};

struct DtcInfo {
  std::string protocol;
  // These masks come from the actual read operation; never infer them from
  // codes in a diagnostic session or the absence of DTC entries.
  std::uint8_t status_availability_mask{0U};
  std::uint8_t requested_status_mask{0U};
  std::vector<DtcEntry> entries;
};

// Implementations must query authoritative application state; no facade method
// is permitted to dispatch commands to CAN, Bench Agent or DUT hardware.
class IReadModel {
 public:
  virtual ~IReadModel() = default;
  [[nodiscard]] virtual ReadResult<PlatformInfo> platform() const {
    return {};
  }
  [[nodiscard]] virtual ReadResult<InterfacesInfo> interfaces() const {
    return {};
  }
  [[nodiscard]] virtual ReadResult<BenchInfo> bench() const {
    return {};
  }
  [[nodiscard]] virtual ReadResult<DutInfo> dut() const {
    return {};
  }
  [[nodiscard]] virtual ReadResult<CapabilitiesInfo> capabilities() const {
    return {};
  }
  [[nodiscard]] virtual ReadResult<DtcInfo> dtcs() const {
    return {};
  }
};

class UnavailableReadModel final : public IReadModel {};

}  // namespace ecu::api::v1
