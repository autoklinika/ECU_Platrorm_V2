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

bool valid(const PlatformInfo& value) {
  return !value.product.empty() && !value.version.empty();
}

bool valid(const InterfacesInfo& value) {
  for (const auto& item : value.interfaces) {
    if (item.name.empty() || (item.fd_enabled && item.data_bitrate == 0U)) {
      return false;
    }
  }
  return true;
}

bool valid(const BenchInfo& value) {
  return !value.configured || value.profile_id != 0U;
}

bool valid(const DutInfo& value) {
  return value.profile_id != 0U;
}

bool valid(const CapabilitiesInfo&) {
  return true;
}

bool valid(const DtcInfo& value) {
  if (value.protocol.empty()) {
    return false;
  }
  for (const auto& entry : value.entries) {
    if (entry.code.empty()) {
      return false;
    }
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
                  ",\"entries\":["};
  for (const auto& entry : value.entries) {
    if (out.back() != '[') out += ',';
    out += "{\"code\":" + json_string(entry.code) +
           ",\"status_mask\":" + std::to_string(entry.status_mask) + "}";
  }
  return out + "]}";
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
  constexpr std::array<std::string_view, 7> paths{
      "/api/v1/about", "/api/v1/platform", "/api/v1/interfaces",
      "/api/v1/bench/session", "/api/v1/dut",
      "/api/v1/dut/capabilities", "/api/v1/dut/dtcs"};
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
  } else {
    response = error(404, "not_found");
  }
  if (!request.origin.empty()) response.cors_origin = allowed_origin_;
  return response;
}

}  // namespace ecu::api::v1
