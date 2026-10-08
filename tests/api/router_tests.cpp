#include "ecu/api/projections.hpp"
#include "ecu/api/router.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

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
    return {ReadStatus::ok, {{{"can-test", true, false, true, false, 500000U, 2000000U}}}};
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
    return {ReadStatus::ok, {"uds", 0x8bU, 0xffU, {{"ABC", 0x8bU}}}};
  }
};

// Corrupt snapshots represent a buggy backend, not synthetic ECU state.
// The router must never reinterpret them as successful observation.
struct InvalidModel final : IReadModel {
  ReadResult<PlatformInfo> platform() const override {
    return {ReadStatus::ok, {std::string(1U, static_cast<char>(0xff)), "1"}};
  }
  ReadResult<InterfacesInfo> interfaces() const override {
    return {ReadStatus::ok, {{{"can0", true, false, false, false, 0U, 0U}}}};
  }
  ReadResult<BenchInfo> bench() const override {
    return {ReadStatus::ok, {static_cast<BenchPhase>(255U), 42U, 1U, true, false}};
  }
  ReadResult<DutInfo> dut() const override {
    return {ReadStatus::ok, {42U, static_cast<DutKind>(255U), "undefined-kind"}};
  }
  ReadResult<CapabilitiesInfo> capabilities() const override {
    return {ReadStatus::ok, {{static_cast<ReadCapability>(255U)}}};
  }
  ReadResult<DtcInfo> dtcs() const override {
    return {ReadStatus::ok, {"uds", 0x8bU, 0xffU, {{"<script>", 0x01U}}}};
  }
};

struct OversizedModel final : IReadModel {
  ReadResult<PlatformInfo> platform() const override {
    return {ReadStatus::ok, {std::string(129U, 'x'), "v1"}};
  }
  ReadResult<InterfacesInfo> interfaces() const override {
    return {ReadStatus::ok, {{{"can0", true, false, false, false, 500000U, 0U},
                              {"can0", true, false, false, false, 500000U, 0U}}}};
  }
  ReadResult<CapabilitiesInfo> capabilities() const override {
    return {ReadStatus::ok, {{ReadCapability::dtc_read, ReadCapability::dtc_read}}};
  }
  ReadResult<DtcInfo> dtcs() const override {
    DtcInfo value{"uds", 0x8bU, 0xffU, {}};
    value.entries.resize(257U, {"P0001", 1U});
    return {ReadStatus::ok, std::move(value)};
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
  test.target = "/api/v1/interfaces";
  check(router.route(test).body.find("\"bus_off\":false") !=
        std::string::npos, "bus-off-preserved");
  InvalidModel invalid{};
  const Router invalid_router{invalid, token, 8878U};
  for (const auto& path : paths) {
    test = good;
    test.target = path;
    check(invalid_router.route(test).status == 502,
          "corrupt-model-must-fail-closed");
  }
  OversizedModel oversized{};
  const Router oversized_router{oversized, token, 8878U};
  for (const auto* path : {"/api/v1/platform", "/api/v1/interfaces",
                           "/api/v1/dut/capabilities", "/api/v1/dut/dtcs"}) {
    test = good;
    test.target = path;
    check(oversized_router.route(test).status == 502,
          "unbounded-or-duplicate-model-must-fail-closed");
  }

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
  existing.schema_version =
      ecu::bench::BenchSessionSnapshot::kSchemaVersion;
  existing.state = static_cast<ecu::bench::BenchSessionState>(255U);
  check(project_bench(existing).status == ReadStatus::invalid_snapshot,
        "unknown-native-state-rejected");
  existing.state = ecu::bench::BenchSessionState::unconfigured;
  check(project_bench(existing).status == ReadStatus::invalid_snapshot,
        "invalid-native-lifecycle-rejected");
  std::cout << "ECU_API_ROUTER=PASS\n";
  return 0;
}
