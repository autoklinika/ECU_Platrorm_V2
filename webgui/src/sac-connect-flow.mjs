// Browser-only interaction state. No device access, no implicit retries.
export class SacConnectionFlow {
  #version = 0;
  #onChange;
  phase = "idle";
  identity = null;
  failure = null;

  constructor(onChange = () => {}) { this.#onChange = onChange; }
  reset() {
    this.#version++;
    this.phase = "idle";
    this.identity = null;
    this.failure = null;
    this.#onChange(this.phase);
  }
  async begin(request) {
    const version = ++this.#version;
    this.identity = null;
    this.failure = null;
    this.phase = "connecting";
    this.#onChange(this.phase);
    try {
      const identity = await request();
      if (version !== this.#version) return;
      this.identity = identity;
      this.phase = "identified";
    } catch (error) {
      if (version !== this.#version) return;
      this.failure = error;
      this.phase = "failed";
    }
    this.#onChange(this.phase);
  }
  updateParameters(identity) {
    if (this.phase !== "accepted" || !this.identity || !identity ||
        this.identity.profile_id !== identity.profile_id ||
        this.identity.bitrate !== identity.bitrate ||
        this.identity.vin_status !== identity.vin_status ||
        this.identity.vin !== identity.vin ||
        this.identity.software !== identity.software ||
        this.identity.hardware !== identity.hardware) return false;
    // Preserve accepted route; never re-open the identification dialog
    // for an automatic read-only refresh from the same physical DUT.
    this.identity = identity;
    return true;
  }
  accept() {
    if (this.phase !== "identified" || !this.identity) return false;
    this.phase = "accepted";
    this.#onChange(this.phase);
    return true;
  }
}
