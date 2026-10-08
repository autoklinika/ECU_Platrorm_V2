#pragma once

#include "ecu/api/read_model.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ecu::api::v1 {

struct Request {
  std::string method;
  std::string target;
  std::string host;
  std::string origin;
  std::string authorization;
  std::string preflight_method;
  std::string preflight_headers;
  bool malformed{false};
  bool body_present{false};
};

struct Response {
  int status{500};
  std::string body;
  std::string cors_origin;
  bool preflight{false};
  bool auth_challenge{false};
  bool method_not_allowed{false};

  Response() = default;
  Response(int code, std::string payload, std::string origin = "",
           bool cors_preflight = false, bool challenge = false,
           bool no_method = false)
      : status(code), body(std::move(payload)),
        cors_origin(std::move(origin)), preflight(cors_preflight),
        auth_challenge(challenge), method_not_allowed(no_method) {}
};

class Router final {
 public:
  Router(const IReadModel& model,
         std::string token,
         std::uint16_t listen_port,
         std::string allowed_origin = "http://127.0.0.1:8877");

  [[nodiscard]] bool configured() const noexcept;
  [[nodiscard]] Response route(const Request& request) const;
  [[nodiscard]] static bool valid_token(std::string_view token) noexcept;

 private:
  [[nodiscard]] bool authenticated(std::string_view authorization) const noexcept;
  [[nodiscard]] bool known_path(std::string_view path) const noexcept;
  [[nodiscard]] static Response error(int status, const char* code);

  const IReadModel& model_;
  const std::string token_;
  const std::string host_;
  const std::string allowed_origin_;
};

}  // namespace ecu::api::v1
