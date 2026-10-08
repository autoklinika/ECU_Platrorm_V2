#include "ecu/api/http_transport.hpp"
#include "ecu/api/read_model.hpp"
#include "ecu/api/router.hpp"
#ifdef ECU_API_HAVE_LINUX_LINK
#include "ecu/api/linux_read_model.hpp"
#endif

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif
#ifdef ECU_API_HAVE_LINUX_LINK
#include <grp.h>
#include <pwd.h>
#endif

namespace {

bool read_token(const std::string& path, std::string& token) {
#ifndef _WIN32
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0 ||
      !S_ISREG(info.st_mode) ||
      (info.st_mode & (S_IWGRP | S_IRWXO)) != 0) return false;
#endif
  std::ifstream file(path);
  if (!file || !std::getline(file, token)) return false;
  if (!token.empty() && token.back() == '\r') token.pop_back();
  std::string trailing;
  if (std::getline(file, trailing)) return false;
  return ecu::api::v1::Router::valid_token(token);
}

bool parse_port(const std::string& text, std::uint16_t& port) {
  if (text.empty() || text.size() > 5U) return false;
  unsigned int n = 0U;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') return false;
    n = n * 10U + static_cast<unsigned>(ch - '0');
  }
  if (n == 0U || n > 65535U) return false;
  port = static_cast<std::uint16_t>(n);
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string token_file;
  std::string can_interface;
  std::string readout_directory;
  std::string readout_owner;
  std::string readout_group;
  std::uint16_t port = 8878U;
  for (int index = 1; index < argc; ++index) {
    const std::string key{argv[index]};
    if (index + 1 >= argc) {
      std::cerr << "ECU_API_ARGS=INVALID\n";
      return 2;
    }
    const std::string value{argv[++index]};
    if (key == "--token-file") {
      token_file = value;
    } else if (key == "--port") {
      if (!parse_port(value, port)) return 2;
    } else if (key == "--can-interface") {
      can_interface = value;
    } else if (key == "--readout-dir") {
      readout_directory = value;
    } else if (key == "--readout-owner") {
      readout_owner = value;
    } else if (key == "--readout-group") {
      readout_group = value;
    } else {
      std::cerr << "ECU_API_ARGS=INVALID\n";
      return 2;
    }
  }

  std::string token;
  if (token_file.empty() || !read_token(token_file, token)) {
    std::cerr << "ECU_API_TOKEN=UNAVAILABLE\n";
    return 2;
  }
  std::unique_ptr<ecu::api::v1::IReadModel> model;
#ifdef ECU_API_HAVE_LINUX_LINK
  const bool readout_enabled = !readout_directory.empty() ||
      !readout_owner.empty() || !readout_group.empty();
  uid_t producer_uid = 0;
  gid_t reader_gid = 0;
  if (readout_enabled) {
    if (readout_directory.empty() || readout_owner.empty() ||
        readout_group.empty()) {
      std::cerr << "ECU_API_READOUT_CONFIG=INCOMPLETE\n";
      return 2;
    }
    const auto* owner = ::getpwnam(readout_owner.c_str());
    if (owner == nullptr || owner->pw_uid == 0) {
      std::cerr << "ECU_API_READOUT_CONFIG=INVALID\n";
      return 2;
    }
    producer_uid = owner->pw_uid;
    const auto* group = ::getgrnam(readout_group.c_str());
    if (group == nullptr) {
      std::cerr << "ECU_API_READOUT_CONFIG=INVALID\n";
      return 2;
    }
    reader_gid = group->gr_gid;
  }
  if (!can_interface.empty() || readout_enabled) {
    model = std::make_unique<ecu::api::v1::LinuxLinkReadModel>(
        can_interface, readout_directory, producer_uid, reader_gid);
  }
#else
  if (!can_interface.empty() || !readout_directory.empty() ||
      !readout_owner.empty() || !readout_group.empty()) {
    std::cerr << "ECU_API_PLATFORM=UNSUPPORTED\n";
    return 2;
  }
#endif
  if (!model)
    model = std::make_unique<ecu::api::v1::UnavailableReadModel>();
  const ecu::api::v1::Router router{*model, token, port};
  if (!router.configured()) return 2;
  return ecu::api::v1::serve_loopback(router, port);
}
