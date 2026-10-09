// Screen-scoped, sequential SAC measurement polling.
// Identification is completed ONCE at connect. Each cycle invokes only
// the dedicated native FE96/FEAE read for the selected backend DUT session.
// WebGUI cannot access CAN or initiate generic commands.
const sameProfile = (a,b) => a && b &&
  a.profile_id === b.profile_id && a.bitrate === b.bitrate;

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
               clock = () => Date.now(),
               schedule = (callback, ms) => globalThis.setTimeout(callback, ms),
               cancel = (timerId) => globalThis.clearTimeout(timerId)}) {
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
    this.#lastCapture = 0;
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
      const {operation, parameters} = await this.#read();
      if (!this.#active || epoch !== this.#epoch) return;
      // Verify session/profile, not ECU identification (which belongs ONLY
      // to the connection stage).
      if (!sameProfile(this.#identity, operation)) {
        this.#emit({status:"profile_mismatch"});
        this.stop();
        return;
      }
      if (!operation.parameters_published ||
          operation.parameters_status !== "completed") {
        this.#emit({status:operation.parameters_status === "timeout" ? "timeout" : "unavailable",
                    operation});
        return;
      }
      if (!parameters || parameters.live !== false ||
          parameters.source !== "completed_application_operation" ||
          parameters.profile_id !== operation.profile_id ||
          parameters.captured_at_unix_ms !== operation.parameter_captured_at_unix_ms ||
          parameters.completed_generation !== operation.parameter_completed_generation ||
          parameters.captured_at_unix_ms <= this.#lastCapture ||
          parameters.captured_at_unix_ms < this.#clock() - 6000 ||
          parameters.captured_at_unix_ms > this.#clock() + 2000) {
        this.#emit({status:"invalid_readout", operation});
        return;
      }
      this.#lastCapture = parameters.captured_at_unix_ms;
      success = true;
      this.#emit({status:"updated", operation, parameters});
    } catch (error) {
      if (this.#active && epoch === this.#epoch) {
        if (error?.code === "session_expired") {
          this.#emit({status:"session_expired", error});
          this.stop();
        } else {
          this.#emit({status:"error", error});
        }
      }
    } finally {
      this.#busy = false;
      if (this.#active) this.#queue(success ? this.#delay : this.#retryDelay);
    }
  }
}
