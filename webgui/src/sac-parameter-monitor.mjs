// Screen-scoped, sequential SAC measurement polling.
// Each cycle uses the EXISTING read-only, auto-bitrate native connection adapter.
// No CAN access here, no parallel probes and no device commands from WebGUI.
const sameDut = (a, b) => a && b &&
  a.profile_id === b.profile_id && a.bitrate === b.bitrate &&
  a.vin_status === b.vin_status && a.vin === b.vin &&
  a.software === b.software && a.hardware === b.hardware;

export class SacParameterMonitor {
  #read;
  #emit;
  #delay;
  #retryDelay;
  #clock;
  #schedule;
  #cancel;
  #timer = null;
  #active = false;
  #busy = false;
  #epoch = 0;
  #identity = null;
  #lastCapture = 0;

  constructor({read, onUpdate, intervalMs = 1200, retryMs = 4000,
               clock = () => Date.now(), schedule = setTimeout,
               cancel = clearTimeout}) {
    if (typeof read !== "function" || typeof onUpdate !== "function")
      throw new TypeError("Invalid SAC monitor callbacks");
    this.#read = read;
    this.#emit = onUpdate;
    this.#delay = intervalMs;
    this.#retryDelay = retryMs;
    this.#clock = clock;
    this.#schedule = schedule;
    this.#cancel = cancel;
  }

  get active() { return this.#active; }
  get busy() { return this.#busy; }

  start(identity) {
    if (this.#active) return;
    if (!identity || ![250000, 500000].includes(identity.bitrate) ||
        identity.profile_id !== (identity.bitrate === 250000 ?
          0xDAF00025 : 0xDAF00050)) throw new TypeError("Invalid SAC identity");
    this.#active = true;
    ++this.#epoch;
    this.#identity = identity;
    this.#lastCapture = identity.parameter_captured_at_unix_ms ?? 0;
    this.#queue(0);
  }

  stop() {
    this.#active = false;
    ++this.#epoch;
    if (this.#timer !== null) this.#cancel(this.#timer);
    this.#timer = null;
  }

  refreshNow() {
    if (!this.#active || this.#busy) return false;
    if (this.#timer !== null) this.#cancel(this.#timer);
    this.#timer = null;
    this.#queue(0);
    return true;
  }

  #queue(ms) {
    if (!this.#active || this.#timer !== null) return;
    this.#timer = this.#schedule(() => {
      this.#timer = null;
      void this.#tick();
    }, ms);
  }

  async #tick() {
    if (!this.#active) return;
    if (this.#busy) {
      this.#queue(250);
      return;
    }
    const epoch = this.#epoch;
    this.#busy = true;
    this.#emit({status:"reading"});
    let success = false;
    try {
      const {identity, parameters} = await this.#read();
      if (!this.#active || epoch !== this.#epoch) return;
      if (!sameDut(this.#identity, identity)) {
        this.#emit({status:"dut_changed"});
        this.stop();
        return;
      }
      this.#identity = identity;
      if (!identity.parameters_published || identity.parameters_status !== "completed") {
        this.#emit({status:identity.parameters_status === "timeout" ? "timeout" : "unavailable",
                    identity});
        return;
      }
      if (!parameters || parameters.live !== false ||
          parameters.source !== "completed_application_operation" ||
          parameters.profile_id !== identity.profile_id ||
          parameters.captured_at_unix_ms !== identity.parameter_captured_at_unix_ms ||
          parameters.completed_generation !== identity.parameter_completed_generation ||
          parameters.captured_at_unix_ms <= this.#lastCapture ||
          parameters.captured_at_unix_ms < this.#clock() - 6000 ||
          parameters.captured_at_unix_ms > this.#clock() + 2000) {
        this.#emit({status:"invalid_readout", identity});
        return;
      }
      this.#lastCapture = parameters.captured_at_unix_ms;
      success = true;
      this.#emit({status:"updated", identity, parameters});
    } catch (error) {
      if (this.#active && epoch === this.#epoch)
        this.#emit({status:"error", error});
    } finally {
      this.#busy = false;
      if (this.#active) this.#queue(success ? this.#delay : this.#retryDelay);
    }
  }
}
