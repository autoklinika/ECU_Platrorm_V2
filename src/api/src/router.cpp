#include "ecu/api/router.hpp"

#include <array>
#include <utility>

#ifndef ECU_API_BUILD_REVISION
#define ECU_API_BUILD_REVISION "unknown"
#endif

namespace ecu::api::v1 {
namespace {

std::string json_string(std::string_view value) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out{"\""};
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20U) {
          out += "\\u00";
          out += hex[ch >> 4U];
          out += hex[ch & 15U];
        } else {
          out += static_cast<char>(ch);
        }
        break;
    }
  }
  out += '"';
  return out;
}

const char* phase_key(BenchPhase phase) {
  switch (phase) {
    case BenchPhase::unconfigured: return "unconfigured";
    case BenchPhase::ready: return "ready";
    case BenchPhase::starting: return "starting";
    case BenchPhase::running: return "running";
    case BenchPhase::stopping: return "stopping";
    case BenchPhase::recovering: return "recovering";
    case BenchPhase::faulted: return "faulted";
  }
  return "unknown";
}

const char* kind_key(DutKind kind) {
  switch (kind) {
    case DutKind::ecu: return "ecu";
    case DutKind::actuator: return "actuator";
    case DutKind::sensor: return "sensor";
    case DutKind::gateway: return "gateway";
    case DutKind::network_node: return "network_node";
    case DutKind::other: return "other";
  }
  return "other";
}

const char* capability_key(ReadCapability capability) {
  switch (capability) {
    case ReadCapability::identification: return "identification";
    case ReadCapability::dtc_read: return "dtc_read";
    case ReadCapability::live_parameters: return "live_parameters";
  }
  return "unknown";
}

const char* json_bool(bool value) {
  return value ? "true" : "false";
}

// Application snapshots may be invalid or untrusted. Reject malformed or
// unbounded DTOs before JSON serialization (rather than presenting them as
// a real ECU / session state).
bool bounded_utf8(const std::string_view value, std::size_t maximum) noexcept {
  if (value.empty() || value.size() > maximum) return false;
  std::size_t i = 0U;
  while (i < value.size()) {
    const auto lead = static_cast<unsigned char>(value[i++]);
    if (lead <= 0x7fU) {
      if (lead == 0U || lead == 0x7fU ||
          (lead < 0x20U && lead != '\n' && lead != '\r' && lead != '\t'))
        return false;
      continue;
    }
    unsigned int continuation_count = 0U;
    std::uint32_t point = 0U;
    std::uint32_t minimum = 0U;
    if (lead >= 0xc2U && lead <= 0xdfU) {
      continuation_count = 1U; point = lead & 0x1fU; minimum = 0x80U;
    } else if (lead >= 0xe0U && lead <= 0xefU) {
      continuation_count = 2U; point = lead & 0x0fU; minimum = 0x800U;
    } else if (lead >= 0xf0U && lead <= 0xf4U) {
      continuation_count = 3U; point = lead & 0x07U; minimum = 0x10000U;
    } else {
      return false;
    }
    if (value.size() - i < continuation_count) return false;
    for (unsigned int j = 0U; j < continuation_count; ++j) {
      const auto byte = static_cast<unsigned char>(value[i++]);
      if ((byte & 0xc0U) != 0x80U) return false;
      point = (point << 6U) | static_cast<std::uint32_t>(byte & 0x3fU);
    }
    if (point < minimum || point > 0x10ffffU ||
        (point >= 0xd800U && point <= 0xdfffU))
      return false;
  }
  return true;
}

bool identifier(const std::string_view value, const std::size_t maximum,
                const bool allow_colon = false) noexcept {
  if (value.empty() || value.size() > maximum) return false;
  for (const char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '_' || ch == '-' || ch == '.' ||
        (allow_colon && (ch == ':' || ch == '/')))
      continue;
    return false;
  }
  return true;
}

bool valid(const PlatformInfo& value) {
  return bounded_utf8(value.product, 128U) &&
         bounded_utf8(value.version, 64U);
}

bool valid(const InterfacesInfo& value) {
  if (value.interfaces.size() > 32U) return false;
  for (std::size_t i = 0; i < value.interfaces.size(); ++i) {
    const auto& item = value.interfaces[i];
    if (!identifier(item.name, 15U) ||
        (item.up && item.bitrate == 0U) ||
        (item.fd_enabled && (item.bitrate == 0U || item.data_bitrate == 0U)))
      return false;
    for (std::size_t j = 0; j < i; ++j)
      if (value.interfaces[j].name == item.name) return false;
  }
  return true;
}

bool valid(const BenchInfo& value) {
  return value.configured && value.profile_id != 0U &&
         value.phase != BenchPhase::unconfigured &&
         static_cast<unsigned int>(value.phase) <=
             static_cast<unsigned int>(BenchPhase::faulted);
}

bool valid(const DutInfo& value) {
  return value.profile_id != 0U &&
         static_cast<unsigned int>(value.kind) <=
             static_cast<unsigned int>(DutKind::other) &&
         (value.profile_label.empty() ||
          bounded_utf8(value.profile_label, 128U));
}

bool valid(const CapabilitiesInfo& value) {
  if (value.available_read_operations.size() > 3U) return false;
  for (std::size_t i = 0; i < value.available_read_operations.size(); ++i) {
    if (static_cast<unsigned int>(value.available_read_operations[i]) >
        static_cast<unsigned int>(ReadCapability::live_parameters))
      return false;
    for (std::size_t j = 0; j < i; ++j)
      if (value.available_read_operations[j] ==
          value.available_read_operations[i]) return false;
  }
  return true;
}

bool valid(const DtcInfo& value) {
  if (!identifier(value.protocol, 32U) ||
      value.requested_status_mask == 0U || value.entries.size() > 256U)
    return false;
  for (std::size_t i = 0U; i < value.entries.size(); ++i) {
    const auto& entry = value.entries[i];
    if (!identifier(entry.code, 32U, true) ||
        (entry.status_mask &
         static_cast<std::uint8_t>(~value.status_availability_mask)) != 0U)
      return false;
    for (std::size_t j = 0U; j < i; ++j)
      if (value.entries[j].code == entry.code) return false;
  }
  return true;
}

std::string serialize(const PlatformInfo& value) {
  return "{\"product\":" + json_string(value.product) +
         ",\"version\":" + json_string(value.version) + "}";
}

std::string serialize(const InterfacesInfo& value) {
  std::string out{"{\"interfaces\":["};
  for (const auto& item : value.interfaces) {
    if (out.back() != '[') out += ',';
    out += "{\"name\":" + json_string(item.name);
    out += ",\"up\":";
    out += json_bool(item.up);
    out += ",\"bus_off\":";
    out += json_bool(item.bus_off);
    out += ",\"fd_enabled\":";
    out += json_bool(item.fd_enabled);
    out += ",\"listen_only\":";
    out += json_bool(item.listen_only);
    out += ",\"bitrate\":" + std::to_string(item.bitrate);
    out += ",\"data_bitrate\":" + std::to_string(item.data_bitrate);
    out += "}";
  }
  return out + "]}";
}

std::string serialize(const BenchInfo& value) {
  return std::string{"{\"state\":"} + json_string(phase_key(value.phase)) +
         ",\"profile_id\":" + std::to_string(value.profile_id) +
         ",\"revision\":" + std::to_string(value.revision) +
         ",\"configured\":" + json_bool(value.configured) +
         ",\"cleanup_required\":" + json_bool(value.cleanup_required) +
         "}";
}

std::string serialize(const DutInfo& value) {
  return "{\"profile_id\":" + std::to_string(value.profile_id) +
         ",\"kind\":" + json_string(kind_key(value.kind)) +
         ",\"profile_label\":" + json_string(value.profile_label) + "}";
}

std::string serialize(const CapabilitiesInfo& value) {
  std::string out{"{\"read_operations\":["};
  for (const auto operation : value.available_read_operations) {
    if (out.back() != '[') out += ',';
    out += json_string(capability_key(operation));
  }
  return out + "]}";
}

std::string serialize(const DtcInfo& value) {
  std::string out{"{\"protocol\":" + json_string(value.protocol) +
                  ",\"status_availability_mask\":" +
                  std::to_string(value.status_availability_mask) +
                  ",\"requested_status_mask\":" +
                  std::to_string(value.requested_status_mask) +
                  ",\"entries\":["};
  for (const auto& entry : value.entries) {
    if (out.back() != '[') out += ',';
    out += "{\"code\":" + json_string(entry.code) +
           ",\"status_mask\":" + std::to_string(entry.status_mask) + "}";
  }
  return out + "]}";
}

bool valid(const CompletedDtcReadout& value) {
  return value.captured_at_unix_ms > 0U &&
         value.profile_id != 0U &&
         value.completed_generation > 0U &&
         valid(value.dtcs);
}

std::string serialize(const CompletedDtcReadout& value) {
  return std::string{"{\"source\":\"completed_application_operation\","
                     "\"live\":false,\"captured_at_unix_ms\":"} +
         std::to_string(value.captured_at_unix_ms) +
         ",\"profile_id\":" + std::to_string(value.profile_id) +
         ",\"completed_generation\":" +
         std::to_string(value.completed_generation) +
         ",\"dtcs\":" + serialize(value.dtcs) + "}";
}

template <class T>
Response data_response(const ReadResult<T>& result) {
  switch (result.status) {
    case ReadStatus::backend_unavailable:
      return {503, "{\"schema_version\":1,\"error\":{\"code\":\"backend_unavailable\"}}"};
    case ReadStatus::no_active_session:
      return {404, "{\"schema_version\":1,\"error\":{\"code\":\"no_active_session\"}}"};
    case ReadStatus::unsupported:
      return {501, "{\"schema_version\":1,\"error\":{\"code\":\"unsupported\"}}"};
    case ReadStatus::expired_readout:
      return {410, "{\"schema_version\":1,\"error\":{\"code\":\"readout_expired\"}}"};
    case ReadStatus::invalid_snapshot:
      return {502, "{\"schema_version\":1,\"error\":{\"code\":\"invalid_snapshot\"}}"};
    case ReadStatus::ok:
      if (!valid(result.value)) {
        return {502, "{\"schema_version\":1,\"error\":{\"code\":\"invalid_snapshot\"}}"};
      }
      return {200, "{\"schema_version\":1,\"data\":" + serialize(result.value) + "}"};
  }
  return {502, "{\"schema_version\":1,\"error\":{\"code\":\"invalid_snapshot\"}}"};
}

}  // namespace

Router::Router(const IReadModel& model, std::string token,
               const std::uint16_t listen_port, std::string allowed_origin)
    : model_(model),
      token_(std::move(token)),
      host_("127.0.0.1:" + std::to_string(listen_port)),
      allowed_origin_(std::move(allowed_origin)) {}

bool Router::valid_token(const std::string_view token) noexcept {
  if (token.size() != 64U) return false;
  for (const char ch : token) {
    if (!((ch >= '0' && ch <= '9') ||
          (ch >= 'a' && ch <= 'f'))) return false;
  }
  return true;
}

bool Router::configured() const noexcept {
  return valid_token(token_) &&
         allowed_origin_ == "http://127.0.0.1:8877" &&
         host_ != "127.0.0.1:0";
}

bool Router::authenticated(const std::string_view authorization) const noexcept {
  constexpr std::string_view prefix{"Bearer "};
  if (!configured() || authorization.size() != prefix.size() + token_.size())
    return false;
  // Compare every byte rather than short-circuiting on the first mismatch.
  unsigned int difference = 0U;
  for (std::size_t i = 0; i < prefix.size(); ++i)
    difference |= static_cast<unsigned char>(authorization[i] ^ prefix[i]);
  for (std::size_t i = 0; i < token_.size(); ++i)
    difference |= static_cast<unsigned char>(
        authorization[i + prefix.size()] ^ token_[i]);
  return difference == 0U;
}

bool Router::known_path(std::string_view path) const noexcept {
  constexpr std::array<std::string_view, 8> paths{
      "/api/v1/about", "/api/v1/platform", "/api/v1/interfaces",
      "/api/v1/bench/session", "/api/v1/dut",
      "/api/v1/dut/capabilities", "/api/v1/dut/dtcs",
      "/api/v1/readouts/dtc/latest"};
  for (const auto allowed : paths)
    if (path == allowed) return true;
  return false;
}

Response Router::error(const int status, const char* code) {
  return {status, std::string{"{\"schema_version\":1,\"error\":{\"code\":"} +
                      json_string(code) + "}}"};
}

Response Router::route(const Request& request) const {
  if (!configured()) return error(503, "api_not_configured");
  if (request.malformed || request.body_present)
    return error(400, "invalid_request");
  if (request.host != host_) return error(403, "invalid_host");
  if (!request.origin.empty() && request.origin != allowed_origin_)
    return error(403, "origin_forbidden");
  if (!known_path(request.target)) return error(404, "not_found");

  if (request.method == "OPTIONS") {
    if (request.origin != allowed_origin_ || request.preflight_method != "GET" ||
        (request.preflight_headers != "authorization" &&
         request.preflight_headers != "Authorization"))
      return error(403, "preflight_denied");
    return {204, "", allowed_origin_, true};
  }

  if (request.method != "GET") {
    Response response = error(405, "method_not_allowed");
    response.method_not_allowed = true;
    return response;
  }
  if (!authenticated(request.authorization)) {
    Response response = error(401, "unauthorized");
    response.auth_challenge = true;
    if (!request.origin.empty()) response.cors_origin = allowed_origin_;
    return response;
  }

  Response response{};
  if (request.target == "/api/v1/about") {
    response = {200, std::string{
        "{\"schema_version\":1,\"data\":{\"api_version\":\"v1\","
        "\"read_only\":true,\"build_revision\":"} +
        json_string(ECU_API_BUILD_REVISION) + "}}"};
  } else if (request.target == "/api/v1/platform") {
    response = data_response(model_.platform());
  } else if (request.target == "/api/v1/interfaces") {
    response = data_response(model_.interfaces());
  } else if (request.target == "/api/v1/bench/session") {
    response = data_response(model_.bench());
  } else if (request.target == "/api/v1/dut") {
    response = data_response(model_.dut());
  } else if (request.target == "/api/v1/dut/capabilities") {
    response = data_response(model_.capabilities());
  } else if (request.target == "/api/v1/dut/dtcs") {
    response = data_response(model_.dtcs());
  } else if (request.target == "/api/v1/readouts/dtc/latest") {
    response = data_response(model_.latest_completed_dtcs());
  } else {
    response = error(404, "not_found");
  }
  if (!request.origin.empty()) response.cors_origin = allowed_origin_;
  return response;
}

}  // namespace ecu::api::v1
