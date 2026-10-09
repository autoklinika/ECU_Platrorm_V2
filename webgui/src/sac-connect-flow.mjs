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
      // The request may await an older native probe before starting.
      // Do not start hardware work if the operator canceled meanwhile.
      const identity = await request(() => version === this.#version);
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
  accept() {
    if (this.phase !== "identified" || !this.identity) return false;
    this.phase = "accepted";
    this.#onChange(this.phase);
    return true;
  }
}
