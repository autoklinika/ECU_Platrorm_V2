import test from "node:test";
import assert from "node:assert/strict";
import {
  API_ROOT, ApiError, ApiSession, readOnlyRequest, SESSION_LIFETIME_MS
} from "../src/api-client.mjs";

const token = "a".repeat(64);
const about = { api_version: "v1", read_only: true, build_revision: "test-123" };
const interfaces = { interfaces: [{
  name: "can0", up: false, bus_off: false, fd_enabled: false,
  listen_only: false, bitrate: 500000, data_bitrate: 0
}] };
const readout = () => ({
  source: "completed_application_operation", live: false,
  captured_at_unix_ms: Date.now() - 10000,
  profile_id: 3667988560, completed_generation: 2,
  dtcs: { protocol: "uds", requested_status_mask: 255,
    status_availability_mask: 139, entries: [
      { code: "12ab3f", status_mask: 0x8b }
    ] }
});

function reply(status, contents, contentType = "application/json") {
  const payload = status >= 400
    ? { schema_version: 1, error: { code: contents } }
    : { schema_version: 1, data: contents };
  return { status, ok: status >= 200 && status < 300, redirected: false,
    headers: { get: () => contentType }, text: async () => JSON.stringify(payload) };
}

function mock(routes, seen = []) {
  return async (url, options) => {
    seen.push({ url, options });
    return routes[url.replace(API_ROOT, "")] ??
      reply(404, "not_found");
  };
}

test("strict allowlist and token format: no arbitrary routes or commands", async () => {
  const fake = mock({ "/api/v1/about": reply(200, about) });
  await assert.rejects(readOnlyRequest("/api/v1/clear", token, fake), {
    code: "invalid_client_request"
  });
  await assert.rejects(readOnlyRequest("/api/v1/about", "not-a-token", fake), {
    code: "invalid_client_request"
  });
  await assert.rejects(readOnlyRequest("http://evil.test/", token, fake), {
    code: "invalid_client_request"
  });
});

test("strict fetch isolation and Authorization header; no ambient credentials", async () => {
  const seen = [];
  const data = await readOnlyRequest("/api/v1/about", token,
    mock({ "/api/v1/about": reply(200, about) }, seen));
  assert.deepEqual(data, about);
  assert.equal(seen.length, 1);
  assert.equal(seen[0].url, API_ROOT + "/api/v1/about");
  assert.deepEqual(Object.keys(seen[0].options.headers).sort(), ["Accept", "Authorization"]);
  assert.equal(seen[0].options.headers.Authorization, "Bearer " + token);
  assert.equal(seen[0].options.method, "GET");
  assert.equal(seen[0].options.credentials, "omit");
  assert.equal(seen[0].options.cache, "no-store");
  assert.equal(seen[0].options.redirect, "error");
  assert.equal(seen[0].options.referrerPolicy, "no-referrer");
});

test("authenticator stores token in private JS memory and clears on sign-out", async () => {
  const client = new ApiSession(mock({
    "/api/v1/about": reply(200, about),
    "/api/v1/interfaces": reply(200, interfaces)
  }));
  assert.equal(client.connected, false);
  await client.signIn(token);
  assert.equal(client.connected, true);
  assert.ok(client.expiresAt > Date.now());
  assert.ok(client.expiresAt <= Date.now() + SESSION_LIFETIME_MS);
  assert.equal((await client.read("/api/v1/interfaces")).interfaces[0].up, false);
  client.signOut();
  assert.equal(client.expiresAt, 0);
  await assert.rejects(client.read("/api/v1/interfaces"), { code: "session_expired" });
});

test("wrong token, 401 and 403 revoke session and cannot leave stale auth", async () => {
  for (const status of [401, 403]) {
    const client = new ApiSession(mock({
      "/api/v1/about": reply(200, about),
      "/api/v1/interfaces": reply(status, status === 401 ? "unauthorized" : "invalid_host")
    }));
    await client.signIn(token);
    await assert.rejects(client.read("/api/v1/interfaces"), { status });
    assert.equal(client.connected, false);
  }
  const unauthenticated = new ApiSession(mock({
    "/api/v1/about": reply(401, "unauthorized")
  }));
  await assert.rejects(unauthenticated.signIn(token), { status: 401 });
  assert.equal(unauthenticated.connected, false);
});

test("503/410 are not empty DTC lists or successful reads", async () => {
  for (const [status, code] of [[503, "backend_unavailable"], [410, "readout_expired"]]) {
    await assert.rejects(readOnlyRequest("/api/v1/readouts/dtc/latest", token,
      mock({ "/api/v1/readouts/dtc/latest": reply(status, code) })),
      { status, code });
  }
});

test("kernel interfaces: present DOWN is not an ECU connection", async () => {
  const result = await readOnlyRequest("/api/v1/interfaces", token,
    mock({ "/api/v1/interfaces": reply(200, interfaces) }));
  assert.equal(result.interfaces[0].up, false);
  assert.equal(result.interfaces[0].bitrate, 500000);
  await assert.rejects(readOnlyRequest("/api/v1/interfaces", token, mock({
    "/api/v1/interfaces": reply(200, { interfaces: [
      interfaces.interfaces[0], interfaces.interfaces[0]
    ] })
  })), { code: "invalid_response" });
});

test("completed readout is explicitly historical and bounded", async () => {
  const received = await readOnlyRequest("/api/v1/readouts/dtc/latest", token,
    mock({ "/api/v1/readouts/dtc/latest": reply(200, readout()) }));
  assert.equal(received.live, false);
  assert.equal(received.source, "completed_application_operation");
  assert.deepEqual(received.dtcs.entries, [{ code: "12AB3F", status_mask: 0x8b }]);
  for (const mutate of [
    (v) => { v.live = true; },
    (v) => { v.source = "live_can"; },
    (v) => { v.dtcs.entries.push(v.dtcs.entries[0]); },
    (v) => { v.dtcs.entries[0].status_mask = 0xff; },
    (v) => { v.captured_at_unix_ms = Date.now() - 86400001; },
    (v) => { v.captured_at_unix_ms = Date.now() + 3600000; },
    (v) => { v.dtcs.entries[0].code = "<script>"; }
  ]) {
    const bad = readout();
    mutate(bad);
    await assert.rejects(readOnlyRequest("/api/v1/readouts/dtc/latest", token,
      mock({ "/api/v1/readouts/dtc/latest": reply(200, bad) })),
      { code: "invalid_response" });
  }
});

test("fail closed for malformed JSON, invalid content type and schema", async () => {
  for (const response of [
    reply(200, about, "text/html"),
    reply(200, { ...about, read_only: false }),
    { ...reply(200, about), text: async () => "{invalid" },
    { ...reply(200, about), text: async () => "X".repeat(33000) },
    { ...reply(200, about), text: async () => JSON.stringify({ schema_version: 2, data: about }) },
    { ...reply(200, about), redirected: true }
  ]) {
    await assert.rejects(readOnlyRequest("/api/v1/about", token,
      mock({ "/api/v1/about": response })), { code: "invalid_response" });
  }
});

test("logging out during a pending response drops the prior result", async () => {
  let resolve;
  const delayed = new Promise((r) => { resolve = r; });
  const client = new ApiSession(async (url) =>
    url.endsWith("/about") ? reply(200, about) : delayed);
  await client.signIn(token);
  const pending = client.read("/api/v1/interfaces");
  client.signOut();
  resolve(reply(200, interfaces));
  await assert.rejects(pending, { code: "session_changed" });
  assert.equal(client.connected, false);
});

function parameterReadout() {
  return {
    source: "completed_application_operation", live: false,
    captured_at_unix_ms: Date.now() - 5000,
    profile_id: 0xDAF00050, completed_generation: 3,
    parameters: {
      permanent_voltage_v: 27.9, ignition_voltage_v: 27.9,
      pressure1_bar: null, pressure2_bar: null, pgn_feae_observed: true
    }
  };
}

test("SAC parameter readout is historical and preserves unavailable pressure", async () => {
  const path = "/api/v1/readouts/daf-sac/parameters/latest";
  const result = await readOnlyRequest(path, token,
    mock({[path]: reply(200, parameterReadout())}));
  assert.equal(result.live, false);
  assert.equal(result.source, "completed_application_operation");
  assert.equal(result.profile_id, 0xDAF00050);
  assert.equal(result.parameters.permanent_voltage_v, 27.9);
  assert.equal(result.parameters.ignition_voltage_v, 27.9);
  assert.equal(result.parameters.pressure1_bar, null);
  assert.equal(result.parameters.pressure2_bar, null);
  for (const [code, status] of [["backend_unavailable",503],["readout_expired",410]]) {
    await assert.rejects(readOnlyRequest(path, token,
      mock({[path]: reply(status, code)})),{status,code});
  }
});

test("SAC parameter validation prevents misleading values and foreign DUTs", async () => {
  const path = "/api/v1/readouts/daf-sac/parameters/latest";
  const mutators = [
    v => {v.live = true;},
    v => {v.source = "live_telemetry";},
    v => {v.profile_id = 0xDEADBEEF;},
    v => {v.parameters.permanent_voltage_v = -1;},
    v => {v.parameters.permanent_voltage_v = 27.999;},
    v => {v.parameters.permanent_voltage_v = 61;},
    v => {v.parameters.ignition_voltage_v = "27.9";},
    v => {v.parameters.pressure1_bar = 100;},
    v => {v.parameters.pressure1_bar = -1;},
    v => {v.parameters.pressure2_bar = 0.001;},
    v => {v.parameters.pgn_feae_observed = false; v.parameters.pressure1_bar = 0;},
    v => {v.captured_at_unix_ms = 1;},
    v => {v.completed_generation = 0;},
    v => {v.parameters.pressure1_bar = "NaN";},
    v => {v.parameters = null;}
  ];
  for (const mutate of mutators) {
    const bad = parameterReadout();
    mutate(bad);
    await assert.rejects(readOnlyRequest(path, token,
      mock({[path]: reply(200, bad)})),{code:"invalid_response"});
  }
  const realZero = parameterReadout();
  realZero.parameters.pressure1_bar = 0;
  assert.equal((await readOnlyRequest(path,token,
    mock({[path]:reply(200,realZero)}))).parameters.pressure1_bar,0);
});

test("no browser or device access required to run portable tests", () => {
  assert.equal(API_ROOT, "http://127.0.0.1:8878");
  assert.equal(typeof ApiError, "function");
});
