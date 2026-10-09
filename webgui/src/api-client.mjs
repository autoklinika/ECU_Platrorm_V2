// Browser-only, read-only ECU Application API V1 transport.
// No privileged agent, CAN/device access, cookie auth or persistent token storage.
export const API_ROOT = "http://127.0.0.1:8878";
export const SESSION_LIFETIME_MS = 15 * 60 * 1000;
const PATHS = new Set([
  "/api/v1/about", "/api/v1/interfaces", "/api/v1/dut",
  "/api/v1/readouts/dtc/latest",
  "/api/v1/readouts/daf-sac/parameters/latest"
]);
const TOKEN_PATTERN = /^[0-9a-f]{64}$/;
const CODE_PATTERN = /^[0-9A-Fa-f]{6}$/;
const MAX_RESPONSE = 32768;

export class ApiError extends Error {
  constructor(code, status = 0) {
    super(code);
    this.name = "ApiError";
    this.code = code;
    this.status = status;
  }
}

const record = (value) => value !== null && typeof value === "object" &&
  !Array.isArray(value);
const integer = (n, max = Number.MAX_SAFE_INTEGER) =>
  Number.isSafeInteger(n) && n >= 0 && n <= max;
const validText = (s, limit = 128) =>
  typeof s === "string" && s.length > 0 && s.length <= limit;
const invalid = () => { throw new ApiError("invalid_response"); };

function checkedData(path, data) {
  if (!record(data)) invalid();
  if (path === "/api/v1/about") {
    if (data.api_version !== "v1" || data.read_only !== true ||
        !validText(data.build_revision, 128)) invalid();
    return { api_version: "v1", read_only: true,
      build_revision: data.build_revision };
  }
  if (path === "/api/v1/interfaces") {
    if (!Array.isArray(data.interfaces) || data.interfaces.length > 32) invalid();
    const names = new Set();
    const interfaces = data.interfaces.map((item) => {
      if (!record(item) || typeof item.name !== "string" ||
          !/^[A-Za-z0-9_.-]{1,15}$/.test(item.name) || names.has(item.name) ||
          typeof item.up !== "boolean" || typeof item.bus_off !== "boolean" ||
          typeof item.fd_enabled !== "boolean" ||
          typeof item.listen_only !== "boolean" ||
          !integer(item.bitrate, 100000000) ||
          !integer(item.data_bitrate, 100000000) ||
          (item.up && item.bitrate === 0) ||
          (item.fd_enabled && (!item.bitrate || !item.data_bitrate))) invalid();
      names.add(item.name);
      return { name: item.name, up: item.up, bus_off: item.bus_off,
        fd_enabled: item.fd_enabled, listen_only: item.listen_only,
        bitrate: item.bitrate, data_bitrate: item.data_bitrate };
    });
    return { interfaces };
  }
  if (path === "/api/v1/dut") {
    if (!integer(data.profile_id) || data.profile_id === 0 ||
        !["ecu", "actuator", "sensor", "gateway", "network_node", "other"].includes(data.kind) ||
        typeof data.profile_label !== "string" ||
        data.profile_label.length > 128) invalid();
    return { profile_id: data.profile_id, kind: data.kind,
      profile_label: data.profile_label };
  }
  if (path === "/api/v1/readouts/dtc/latest") {
    const dtcs = data.dtcs;
    const now = Date.now();
    if (data.source !== "completed_application_operation" ||
        data.live !== false ||
        !integer(data.captured_at_unix_ms) || data.captured_at_unix_ms === 0 ||
        data.captured_at_unix_ms > now + 60000 ||
        now - data.captured_at_unix_ms >= 24 * 60 * 60 * 1000 ||
        !integer(data.profile_id) || data.profile_id === 0 ||
        !integer(data.completed_generation) || data.completed_generation === 0 ||
        !record(dtcs) || !validText(dtcs.protocol, 32) ||
        !/^[A-Za-z0-9_.:/-]+$/.test(dtcs.protocol) ||
        !integer(dtcs.requested_status_mask, 255) ||
        dtcs.requested_status_mask === 0 ||
        !integer(dtcs.status_availability_mask, 255) ||
        !Array.isArray(dtcs.entries) || dtcs.entries.length > 128) invalid();
    const seen = new Set();
    const entries = dtcs.entries.map((item) => {
      if (!record(item) || !CODE_PATTERN.test(item.code) ||
          seen.has(item.code.toUpperCase()) ||
          !integer(item.status_mask, 255) ||
          (item.status_mask & ~dtcs.status_availability_mask) !== 0) invalid();
      seen.add(item.code.toUpperCase());
      return { code: item.code.toUpperCase(), status_mask: item.status_mask };
    });
    return { source: "completed_application_operation", live: false,
      captured_at_unix_ms: data.captured_at_unix_ms,
      profile_id: data.profile_id, completed_generation: data.completed_generation,
      dtcs: { protocol: dtcs.protocol,
        requested_status_mask: dtcs.requested_status_mask,
        status_availability_mask: dtcs.status_availability_mask, entries } };
  }
  if (path === "/api/v1/readouts/daf-sac/parameters/latest") {
    const now = Date.now();
    const values = data.parameters;
    const sacProfiles = new Set([0xDAF00025, 0xDAF00050]);
    const exactFixed = (value, scale, max) =>
      typeof value === "number" && Number.isFinite(value) &&
      value >= 0 && value <= max &&
      Math.abs(value * scale - Math.round(value * scale)) < 0.00001;
    if (data.source !== "completed_application_operation" ||
        data.live !== false ||
        !integer(data.captured_at_unix_ms) ||
        data.captured_at_unix_ms === 0 ||
        data.captured_at_unix_ms > now + 60000 ||
        now - data.captured_at_unix_ms >= 24 * 60 * 60 * 1000 ||
        !sacProfiles.has(data.profile_id) ||
        !integer(data.completed_generation) || data.completed_generation === 0 ||
        !record(values) ||
        !exactFixed(values.permanent_voltage_v, 10, 60) ||
        !exactFixed(values.ignition_voltage_v, 10, 60) ||
        typeof values.pgn_feae_observed !== "boolean" ||
        !(values.pressure1_bar === null ||
          exactFixed(values.pressure1_bar, 100, 20.24)) ||
        !(values.pressure2_bar === null ||
          exactFixed(values.pressure2_bar, 100, 20.24)) ||
        (!values.pgn_feae_observed &&
         (values.pressure1_bar !== null || values.pressure2_bar !== null))) invalid();
    return {
      source: "completed_application_operation", live: false,
      captured_at_unix_ms: data.captured_at_unix_ms,
      profile_id: data.profile_id,
      completed_generation: data.completed_generation,
      parameters: {
        permanent_voltage_v: values.permanent_voltage_v,
        ignition_voltage_v: values.ignition_voltage_v,
        pressure1_bar: values.pressure1_bar,
        pressure2_bar: values.pressure2_bar,
        pgn_feae_observed: values.pgn_feae_observed
      }
    };
  }
  invalid();
}

export async function readOnlyRequest(path, token, fetchImpl = globalThis.fetch) {
  if (!PATHS.has(path) || !TOKEN_PATTERN.test(token) ||
      typeof fetchImpl !== "function") throw new ApiError("invalid_client_request");
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  let response;
  try {
    response = await fetchImpl(API_ROOT + path, {
      method: "GET", mode: "cors", credentials: "omit", cache: "no-store",
      redirect: "error", referrerPolicy: "no-referrer",
      headers: { Accept: "application/json", Authorization: "Bearer " + token },
      signal: controller.signal
    });
    if (response.redirected ||
        !response.headers?.get("content-type")?.toLowerCase().startsWith("application/json")) {
      invalid();
    }
    const body = await response.text();
    if (body.length > MAX_RESPONSE) invalid();
    let payload;
    try { payload = JSON.parse(body); } catch { invalid(); }
    if (!record(payload) || payload.schema_version !== 1) invalid();
    if (!response.ok) {
      if (!record(payload.error) || !validText(payload.error.code, 64) ||
          !/^[a-z_]+$/.test(payload.error.code)) invalid();
      throw new ApiError(payload.error.code, response.status);
    }
    if (response.status !== 200 || !record(payload.data)) invalid();
    return checkedData(path, payload.data);
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError("network_unavailable");
  } finally {
    clearTimeout(timeout);
  }
}

export const SAC_CONNECT_ROOT = "http://127.0.0.1:8879";
export const SAC_CONNECT_PATH = "/api/v1/bench/daf-sac/connect";
const VALID_SAC_PROFILES = new Set([0xDAF00050]);
const cleanIdentity = (data) => {
  if (!record(data) || !VALID_SAC_PROFILES.has(data.profile_id) ||
      !["UNPROGRAMMED_FF17", "VALID_ASCII"].includes(data.vin_status) ||
      typeof data.software !== "string" ||
      !/^[ -~]{1,64}$/.test(data.software) ||
      typeof data.hardware !== "string" ||
      !/^[ -~]{1,64}$/.test(data.hardware) ||
      typeof data.parameters_published !== "boolean" ||
      !integer(data.parameter_capture_floor_ms) ||
      data.parameter_capture_floor_ms === 0 ||
      data.parameter_capture_floor_ms > Date.now() + 60000 ||
      Date.now() - data.parameter_capture_floor_ms > 60000 ||
      !(data.vin === null && data.vin_status === "UNPROGRAMMED_FF17") &&
      !(typeof data.vin === "string" && data.vin_status === "VALID_ASCII" &&
        /^[A-Za-z0-9]{17}$/.test(data.vin))) invalid();
  return {
    profile_id: data.profile_id, vin: data.vin,
    vin_status: data.vin_status, software: data.software.trim(),
    hardware: data.hardware.trim(), parameters_published: data.parameters_published,
    parameter_capture_floor_ms: data.parameter_capture_floor_ms
  };
};

// This single authenticated POST is an explicit operator request for the
// Linux-specific, fixed-DUT read-only adapter. No generic CAN/ECU API.
export async function requestSacIdentity(token, fetchImpl = globalThis.fetch) {
  if (!TOKEN_PATTERN.test(token) || typeof fetchImpl !== "function")
    throw new ApiError("invalid_client_request");
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 60000);
  try {
    const response = await fetchImpl(SAC_CONNECT_ROOT + SAC_CONNECT_PATH, {
      method: "POST", mode: "cors", credentials: "omit", cache: "no-store",
      redirect: "error", referrerPolicy: "no-referrer",
      headers: { Accept: "application/json", Authorization: "Bearer " + token },
      signal: controller.signal
    });
    if (response.redirected ||
        !response.headers?.get("content-type")?.toLowerCase().startsWith("application/json")) invalid();
    const body = await response.text();
    if (body.length > MAX_RESPONSE) invalid();
    let payload;
    try { payload = JSON.parse(body); } catch { invalid(); }
    if (!record(payload) || payload.schema_version !== 1) invalid();
    if (!response.ok) {
      if (!record(payload.error) || !/^[a-z_]{1,64}$/.test(payload.error.code)) invalid();
      throw new ApiError(payload.error.code, response.status);
    }
    if (response.status !== 200) invalid();
    return cleanIdentity(payload.data);
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError("network_unavailable");
  } finally {
    clearTimeout(timeout);
  }
}

export class ApiSession {
  #token = null;
  #generation = 0;
  #expires = 0;
  #fetch;

  constructor(fetchImpl = globalThis.fetch) { this.#fetch = fetchImpl; }

  get connected() {
    if (this.#token !== null && Date.now() >= this.#expires) this.signOut();
    return this.#token !== null;
  }

  get expiresAt() { return this.connected ? this.#expires : 0; }

  signOut() {
    this.#generation++;
    this.#token = null;
    this.#expires = 0;
  }

  async signIn(value) {
    this.signOut();
    if (!TOKEN_PATTERN.test(value)) throw new ApiError("invalid_token_format");
    const version = this.#generation;
    const about = await readOnlyRequest("/api/v1/about", value, this.#fetch);
    if (version !== this.#generation) throw new ApiError("session_changed");
    this.#token = value;
    this.#expires = Date.now() + SESSION_LIFETIME_MS;
    return about;
  }

  async identifySac() {
    if (!this.connected) throw new ApiError("session_expired");
    const version = this.#generation;
    try {
      const result = await requestSacIdentity(this.#token, this.#fetch);
      if (version !== this.#generation || !this.connected) {
        throw new ApiError("session_changed");
      }
      return result;
    } catch (error) {
      if (version !== this.#generation) throw new ApiError("session_changed");
      if (error instanceof ApiError && (error.status === 401 || error.status === 403))
        this.signOut();
      throw error;
    }
  }

  async read(path) {
    if (!this.connected) throw new ApiError("session_expired");
    const version = this.#generation;
    try {
      const result = await readOnlyRequest(path, this.#token, this.#fetch);
      if (version !== this.#generation || !this.connected) {
        throw new ApiError("session_changed");
      }
      return result;
    } catch (error) {
      if (version !== this.#generation) throw new ApiError("session_changed");
      if (error instanceof ApiError && (error.status === 401 || error.status === 403)) {
        this.signOut();
      }
      throw error;
    }
  }
}
// Same-origin kiosk prototype: browser never receives or types a bearer.
// The unprivileged static server forwards ONLY allowlisted requests using
// a systemd-supplied service credential. API V1 remains authenticated.
const KIOSK_ROUTES = new Map([
  ["/api/v1/about", "/kiosk/v1/about"],
  ["/api/v1/interfaces", "/kiosk/v1/interfaces"],
  ["/api/v1/dut", "/kiosk/v1/dut"],
  ["/api/v1/readouts/dtc/latest", "/kiosk/v1/readouts/dtc/latest"],
  ["/api/v1/readouts/daf-sac/parameters/latest",
   "/kiosk/v1/readouts/daf-sac/parameters/latest"]
]);

async function kioskFetch(path, method, fetchImpl) {
  if (typeof fetchImpl !== "function" ||
      !((method === "GET" && KIOSK_ROUTES.has(path)) ||
        (method === "POST" && path === SAC_CONNECT_PATH)))
    throw new ApiError("invalid_client_request");
  const url = method === "GET" ? KIOSK_ROUTES.get(path) :
    "/kiosk/v1/bench/daf-sac/connect";
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), method === "POST" ? 70000 : 4000);
  try {
    const response = await fetchImpl(url, {
      method, credentials: "omit", cache: "no-store", redirect: "error",
      referrerPolicy: "no-referrer",
      headers: {Accept:"application/json", "X-ECU-Kiosk":"v1"},
      signal: controller.signal
    });
    if (response.redirected ||
        !response.headers?.get("content-type")?.toLowerCase().startsWith("application/json"))
      invalid();
    const raw = await response.text();
    if (raw.length > MAX_RESPONSE) invalid();
    let body;
    try { body = JSON.parse(raw); } catch { invalid(); }
    if (!record(body) || body.schema_version !== 1) invalid();
    if (!response.ok) {
      if (!record(body.error) || !/^[a-z_]{1,64}$/.test(body.error.code)) invalid();
      throw new ApiError(body.error.code, response.status);
    }
    if (response.status !== 200 || !record(body.data)) invalid();
    return method === "GET" ? checkedData(path, body.data) : cleanIdentity(body.data);
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError("network_unavailable");
  } finally {
    clearTimeout(timeout);
  }
}

export class KioskSession {
  #fetch;
  constructor(fetchImpl = globalThis.fetch) { this.#fetch = fetchImpl; }
  get connected() { return true; }
  async read(path) { return kioskFetch(path, "GET", this.#fetch); }
  async identifySac() {
    return kioskFetch(SAC_CONNECT_PATH, "POST", this.#fetch);
  }
  signOut() { /* Prototype kiosk has no browser credential to erase. */ }
}
