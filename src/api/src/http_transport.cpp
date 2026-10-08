#include "ecu/api/http_transport.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using ApiSocket = SOCKET;
constexpr ApiSocket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using ApiSocket = int;
constexpr ApiSocket kInvalidSocket = -1;
#endif

namespace ecu::api::v1 {
namespace {

void close_socket(ApiSocket value) {
#ifdef _WIN32
  closesocket(value);
#else
  close(value);
#endif
}

bool receive_request(ApiSocket client, std::string& buffer) {
  constexpr std::size_t kMaxHeaderBytes = 8192U;
  std::array<char, 2048> bytes{};
  while (buffer.size() <= kMaxHeaderBytes) {
    const int count = recv(client, bytes.data(),
                           static_cast<int>(bytes.size()), 0);
    if (count <= 0) return false;
    buffer.append(bytes.data(), static_cast<std::size_t>(count));
    const auto end = buffer.find("\r\n\r\n");
    if (end != std::string::npos) return end <= kMaxHeaderBytes;
  }
  return false;
}

bool valid_header_name(std::string_view name) {
  if (name.empty()) return false;
  for (const unsigned char ch : name) {
    if (!std::isalnum(ch) && ch != '-') return false;
  }
  return true;
}

std::string ascii_lower(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char ch : value)
    out += static_cast<char>(std::tolower(ch));
  return out;
}

std::string_view trim_ows(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1U);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1U);
  return value;
}

bool safe_header_value(std::string_view value) {
  for (const unsigned char ch : value)
    if (ch < 0x20U && ch != '\t') return false;
  return true;
}

Request parse_request(const std::string& raw) {
  Request request{};
  const auto head_end = raw.find("\r\n\r\n");
  if (head_end == std::string::npos || head_end > 8192U) {
    request.malformed = true;
    return request;
  }
  request.body_present = raw.size() != head_end + 4U;
  const std::string_view head{raw.data(), head_end + 2U};
  const auto first_end = head.find("\r\n");
  if (first_end == std::string::npos) {
    request.malformed = true;
    return request;
  }
  const auto line = head.substr(0U, first_end);
  const auto first_space = line.find(' ');
  const auto second_space = line.find(' ', first_space + 1U);
  if (first_space == std::string::npos ||
      second_space == std::string::npos ||
      line.substr(second_space + 1U) != "HTTP/1.1" ||
      first_space == 0U || second_space == first_space + 1U) {
    request.malformed = true;
    return request;
  }
  request.method = std::string{line.substr(0U, first_space)};
  request.target = std::string{
      line.substr(first_space + 1U, second_space - first_space - 1U)};
  if (request.target.size() > 256U ||
      request.target.front() != '/' ||
      request.target.find_first_of(" ?#%\\") != std::string::npos) {
    request.malformed = true;
    return request;
  }

  bool has_host = false, has_origin = false, has_authorization = false;
  bool has_preflight_method = false, has_preflight_headers = false;
  bool has_length = false, has_transfer = false;
  std::size_t offset = first_end + 2U;
  while (offset < head.size()) {
    const auto end = head.find("\r\n", offset);
    if (end == std::string::npos) {
      request.malformed = true;
      break;
    }
    const auto header = head.substr(offset, end - offset);
    offset = end + 2U;
    if (header.empty()) break;
    const auto colon = header.find(':');
    if (colon == std::string::npos ||
        !valid_header_name(header.substr(0U, colon))) {
      request.malformed = true;
      break;
    }
    const auto key = ascii_lower(header.substr(0U, colon));
    const auto value = trim_ows(header.substr(colon + 1U));
    if (!safe_header_value(value)) {
      request.malformed = true;
      break;
    }
    if (key == "host") {
      if (std::exchange(has_host, true)) request.malformed = true;
      request.host = std::string{value};
    } else if (key == "origin") {
      if (std::exchange(has_origin, true)) request.malformed = true;
      request.origin = std::string{value};
    } else if (key == "authorization") {
      if (std::exchange(has_authorization, true)) request.malformed = true;
      request.authorization = std::string{value};
    } else if (key == "access-control-request-method") {
      if (std::exchange(has_preflight_method, true)) request.malformed = true;
      request.preflight_method = std::string{value};
    } else if (key == "access-control-request-headers") {
      if (std::exchange(has_preflight_headers, true)) request.malformed = true;
      request.preflight_headers = std::string{value};
    } else if (key == "content-length") {
      if (std::exchange(has_length, true)) request.malformed = true;
      request.body_present = true;  // All bodies are forbidden, including zero.
    } else if (key == "transfer-encoding") {
      if (std::exchange(has_transfer, true)) request.malformed = true;
      request.body_present = true;
    } else if (key == "expect") {
      request.malformed = true;
    }
  }
  if (!has_host || raw.find('\0') != std::string::npos)
    request.malformed = true;
  return request;
}

std::string response_text(const Response& response) {
  const char* reason = "Error";
  switch (response.status) {
    case 200: reason = "OK"; break;
    case 204: reason = "No Content"; break;
    case 400: reason = "Bad Request"; break;
    case 401: reason = "Unauthorized"; break;
    case 403: reason = "Forbidden"; break;
    case 404: reason = "Not Found"; break;
    case 405: reason = "Method Not Allowed"; break;
    case 413: reason = "Content Too Large"; break;
    case 501: reason = "Not Implemented"; break;
    case 502: reason = "Bad Gateway"; break;
    case 503: reason = "Service Unavailable"; break;
    default: break;
  }
  std::string output = std::string{"HTTP/1.1 "} +
                       std::to_string(response.status) + " " + reason + "\r\n";
  output += "Content-Type: application/json; charset=utf-8\r\n";
  output += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
  output += "Connection: close\r\n";
  output += "Cache-Control: no-store\r\n";
  output += "X-Content-Type-Options: nosniff\r\n";
  output += "X-Frame-Options: DENY\r\n";
  if (response.auth_challenge)
    output += "WWW-Authenticate: Bearer realm=\"ecu-api-v1\"\r\n";
  if (response.method_not_allowed)
    output += "Allow: GET, OPTIONS\r\n";
  if (!response.cors_origin.empty()) {
    output += "Access-Control-Allow-Origin: " + response.cors_origin + "\r\n";
    output += "Vary: Origin\r\n";
  }
  if (response.preflight) {
    output += "Access-Control-Allow-Methods: GET, OPTIONS\r\n";
    output += "Access-Control-Allow-Headers: Authorization\r\n";
    output += "Access-Control-Max-Age: 60\r\n";
  }
  output += "\r\n";
  output += response.body;
  return output;
}

void send_response(ApiSocket client, const Response& response) {
  const auto bytes = response_text(response);
  std::size_t sent = 0U;
  while (sent < bytes.size()) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const int count = send(client, bytes.data() + sent,
        static_cast<int>(bytes.size() - sent), flags);
    if (count <= 0) return;
    sent += static_cast<std::size_t>(count);
  }
}

void set_socket_timeout(ApiSocket client) {
#ifdef _WIN32
  DWORD milliseconds = 2000;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
             reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
#else
  timeval timeout{};
  timeout.tv_sec = 2;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

}  // namespace

int serve_loopback(const Router& router, const std::uint16_t port) {
#ifdef _WIN32
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
  const ApiSocket listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listening == kInvalidSocket) {
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listening, reinterpret_cast<sockaddr*>(&address),
           sizeof(address)) != 0 || listen(listening, 8) != 0) {
    close_socket(listening);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  std::fprintf(stderr, "ECU_API_LISTEN=127.0.0.1:%u\n",
               static_cast<unsigned>(port));
  for (;;) {
    const ApiSocket client = accept(listening, nullptr, nullptr);
    if (client == kInvalidSocket) continue;
    set_socket_timeout(client);
    std::string input;
    if (!receive_request(client, input)) {
      send_response(client, {413,
          "{\"schema_version\":1,\"error\":{\"code\":\"invalid_http_request\"}}"});
    } else {
      send_response(client, router.route(parse_request(input)));
    }
    close_socket(client);
  }
  // The server is intentionally supervised and terminated externally.
}

}  // namespace ecu::api::v1
