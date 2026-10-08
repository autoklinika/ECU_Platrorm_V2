#include "ecu/api/projections.hpp"
#include "ecu/api/router.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ecu::api::v1;

namespace {

void check(bool condition, const char* name) {
  if (!condition) {
    std::cerr << "API_TEST_FAIL=" << name << '\n';
    std::exit(1);
  }
}

struct TestModel final : IReadModel {
  mutable unsigned calls{0};
  ReadResult<PlatformInfo> platform() const override {
    ++calls;
    return {ReadStatus::ok, {"ECU \"Bench\"\n", "test-only"}};
  }
  ReadResult<InterfacesInfo> interfaces() const override {
    ++calls;
    return {ReadStatus::ok, {{{"can-test", true, true, false, 500000U, 2000000U}}}};
  }
  ReadResult<BenchInfo> bench() const override {
    ++calls;
    return {ReadStatus::ok, {BenchPhase::running, 42U, 7U, true, false}};
  }
  ReadResult<DutInfo> dut() const override {
    ++calls;
    return {ReadStatus::ok, {42U, DutKind::actuator, "synthetic-test"}};
  }
  ReadResult<CapabilitiesInfo> capabilities() const override {
    ++calls;
    return {ReadStatus::ok, {{ReadCapability::identification, ReadCapability::dtc_read}}};
  }
  ReadResult<DtcInfo> dtcs() const override {
    ++calls;
    return {ReadStatus::ok, {"uds", {{"ABC", 0x8bU}}}};
  }
};

}  // namespace

int main() {
  TestModel model;
  const std::string token(64U, 'a');
  Router router{model, token, 8878U};
  check(router.configured(), "configured");
  check(!Router::valid_token("short"), "token-weak-denied");
  check(!Router::valid_token(std::string(64U, 'z')), "token-alphabet");
  const Request good{"GET", "/api/v1/about", "127.0.0.1:8878",
                     "http://127.0.0.1:8877", "Bearer " + token, "", ""};
  const auto about = router.route(good);
  check(about.status == 200, "about-ok");
  check(about.body.find("\"read_only\":true") != std::string::npos,
        "read-only");
  check(about.cors_origin == "http://127.0.0.1:8877", "cors-allow");
  check(model.calls == 0U, "about-no-backend");
  auto test = good;
  test.authorization = "Bearer " + std::string(64U, 'b');
  check(router.route(test).status == 401, "wrong-token");
  check(model.calls == 0U, "unauthorized-no-backend");
  test = good;
  test.origin = "https://evil.invalid";
  check(router.route(test).status == 403, "origin-denied");
  test = good;
  test.host = "attacker.invalid";
  check(router.route(test).status == 403, "host-denied");
  test = good;
  test.method = "POST";
  check(router.route(test).status == 405, "write-denied");
  test = good;
  test.body_present = true;
  check(router.route(test).status == 400, "body-denied");
  test = good;
  test.malformed = true;
  check(router.route(test).status == 400, "malformed-denied");
  test = good;
  test.target = "/api/v1/can/transmit";
  check(router.route(test).status == 404, "no-raw-can");
  test = good;
  test.target += "?test=1";
  check(router.route(test).status == 404, "query-denied");
  test = good;
  test.method = "OPTIONS";
  test.authorization.clear();
  test.preflight_method = "GET";
  test.preflight_headers = "authorization";
  const auto preflight = router.route(test);
  check(preflight.status == 204 && preflight.preflight, "valid-preflight");
  test.preflight_method = "POST";
  check(router.route(test).status == 403, "preflight-write-denied");

  const std::string paths[] = {
    "/api/v1/platform", "/api/v1/interfaces", "/api/v1/bench/session",
    "/api/v1/dut", "/api/v1/dut/capabilities", "/api/v1/dut/dtcs"
  };
  for (const auto& path : paths) {
    test = good;
    test.target = path;
    const auto response = router.route(test);
    check(response.status == 200, "typed-data");
    check(response.body.find("\"schema_version\":1") != std::string::npos,
          "schema-version");
  }
  check(model.calls == 6U, "provider-dispatch-count");
  test = good;
  test.target = "/api/v1/platform";
  check(router.route(test).body.find("\\n") != std::string::npos,
        "json-string-escaping");

  UnavailableReadModel unavailable;
  const Router no_backend{unavailable, token, 8878U};
  test = good;
  test.target = "/api/v1/dut/dtcs";
  const auto missing = no_backend.route(test);
  check(missing.status == 503 &&
        missing.body.find("backend_unavailable") != std::string::npos,
        "backend-fail-closed");

  ecu::bench::BenchSessionSnapshot existing{};
  check(project_bench(existing).status == ReadStatus::no_active_session,
        "unconfigured-does-not-imply-live");
  existing.configured = true;
  existing.dut_profile_id = 42U;
  existing.state = ecu::bench::BenchSessionState::faulted;
  existing.lifecycle_revision = 5U;
  auto projected = project_bench(existing);
  check(projected.status == ReadStatus::ok &&
        projected.value.phase == BenchPhase::faulted &&
        projected.value.revision == 5U, "native-bench-projection");
  existing.schema_version = 999U;
  check(project_bench(existing).status == ReadStatus::invalid_snapshot,
        "native-version-rejected");
  std::cout << "ECU_API_ROUTER=PASS\n";
  return 0;
}
