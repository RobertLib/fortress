// Procedural sound effects. Every effect is synthesized ONCE into an
// AudioBuffer and cached; playing a sound only spawns a cheap BufferSource.

class AudioSys {
  constructor() {
    this.ctx = null;
    this.buffers = {};
    this.master = null;
    this.enabled = true;
  }

  // Must be called from a user gesture (browsers block AudioContext otherwise).
  init() {
    if (this.ctx) {
      if (this.ctx.state === "suspended") this.ctx.resume();
      return;
    }
    this.ctx = new (window.AudioContext || window.webkitAudioContext)();
    this.master = this.ctx.createGain();
    this.master.gain.value = 0.5;
    this.master.connect(this.ctx.destination);
    this.generateAll();
  }

  makeBuffer(duration, fn) {
    const sr = this.ctx.sampleRate;
    const len = Math.max(1, Math.floor(duration * sr));
    const buf = this.ctx.createBuffer(1, len, sr);
    const d = buf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = fn(i / sr, i);
    return buf;
  }

  generateAll() {
    const B = (name, dur, fn) => (this.buffers[name] = this.makeBuffer(dur, fn));
    const noise = () => Math.random() * 2 - 1;
    const env = (t, dur, pow = 3) => Math.pow(Math.max(0, 1 - t / dur), pow);
    const saw = (t, f) => 2 * ((t * f) % 1) - 1;
    const sqr = (t, f) => ((t * f) % 1 < 0.5 ? 1 : -1);
    const sin = (t, f) => Math.sin(2 * Math.PI * f * t);

    // --- weapons ---
    B("crossbow", 0.3, (t) => {
      // string twang + wooden thunk
      const snap = noise() * env(t, 0.03, 2) * 0.7;
      const twang = sin(t, 480 * (1 - t * 1.5)) * env(t, 0.09) * 0.5;
      const thunk = sin(t, 110 * (1 - t)) * env(t, 0.22) * 0.6;
      return (snap + twang + thunk) * 0.85;
    });
    B("arbalest", 0.13, (t) => {
      const snap = noise() * env(t, 0.025, 2) * 0.6;
      const twang = sin(t, 520 * (1 - t * 2)) * env(t, 0.06) * 0.45;
      const thunk = sin(t, 130 * (1 - t * 2)) * env(t, 0.1) * 0.5;
      return (snap + twang + thunk) * 0.8;
    });
    B("dagger", 0.16, (t) => {
      // whoosh: filtered-ish noise with moving amplitude
      const sweep = Math.sin(Math.PI * (t / 0.16));
      return noise() * sweep * sweep * 0.35;
    });
    B("empty", 0.06, (t) => sqr(t, 900) * env(t, 0.05, 6) * 0.25);
    B("enemyShot", 0.26, (t) => {
      // enemy crossbow: deeper twang from a distance
      const snap = noise() * env(t, 0.04, 2) * 0.55;
      const twang = sin(t, 360 * (1 - t * 1.5)) * env(t, 0.1) * 0.45;
      const thunk = sin(t, 90 * (1 - t)) * env(t, 0.24) * 0.6;
      return (snap + twang + thunk) * 0.75;
    });

    // --- traps ---
    B("trapClick", 0.16, (t) => {
      // two dry mechanical clicks under the flagstone
      const on = (t % 0.08) < 0.014;
      return on ? (noise() * 0.4 + sqr(t, 1500) * 0.5) * env(t, 0.16, 1) : 0;
    });
    B("arrowHit", 0.09, (t) => {
      // arrow thocking into wood or stone
      return (noise() * 0.5 + sin(t, 190 * (1 - t * 3)) * 0.6) * env(t, 0.07, 3) * 0.7;
    });

    // --- player ---
    B("playerPain", 0.22, (t) => saw(t, 280 - 500 * t) * env(t, 0.22) * 0.5);
    B("playerDeath", 0.9, (t) => {
      const f = 320 * Math.pow(0.12, t / 0.9) + 40;
      return (saw(t, f) * 0.6 + noise() * 0.15) * env(t, 0.9, 1.5) * 0.8;
    });
    B("step", 0.07, (t) => noise() * env(t, 0.05, 4) * 0.12);

    // --- enemies ---
    B("alert", 0.3, (t) => {
      // sharp two-tone shout, "halt!"
      const f = t < 0.14 ? 470 : 350;
      return (sqr(t, f) * 0.5 + noise() * 0.12) * env(t % 0.15, 0.14, 1.5) * 0.55;
    });
    B("enemyPain", 0.18, (t) => sqr(t, 420 - 700 * t) * env(t, 0.18) * 0.35);
    B("enemyDeath", 0.6, (t) => {
      const f = 380 * Math.pow(0.2, t / 0.6) + 60;
      return (saw(t, f) * 0.55 + noise() * 0.2 * env(t, 0.25)) * env(t, 0.6, 1.6) * 0.7;
    });

    // --- bats ---
    B("batAlert", 0.4, (t) => {
      // burst of shrill chittering as the swarm takes wing
      const on = (t % 0.08) < 0.038;
      const f = 2300 - 900 * (t / 0.4) + sin(t, 27) * 160;
      const flap = noise() * 0.14 * env(t, 0.4, 1);
      return (on ? sin(t, f) * env(t % 0.08, 0.05, 2) * 0.4 : 0) + flap;
    });
    B("batSqueak", 0.14, (t) => sin(t, 2600 - 5000 * t) * env(t, 0.13, 2) * 0.4);
    B("batBite", 0.12, (t) => (sin(t, 1900 - 6500 * t) * 0.5 + noise() * 0.18) * env(t, 0.11, 2) * 0.6);
    B("batDeath", 0.5, (t) => {
      const f = 2200 * Math.pow(0.22, t / 0.5);
      return (sin(t, f) * 0.5 + noise() * 0.15 * env(t, 0.22)) * env(t, 0.5, 1.5) * 0.6;
    });
    B("flutter", 0.24, (t) => {
      // leathery wingbeats: noise pulsed at wing frequency
      const beat = Math.pow(Math.max(0, Math.sin(2 * Math.PI * 13 * t)), 2);
      return noise() * beat * env(t, 0.24, 1) * 0.3;
    });

    // --- pickups ---
    B("pickup", 0.14, (t) => sin(t, 620 + 1800 * t) * env(t, 0.14) * 0.4);
    B("pickupAmmo", 0.12, (t) => sqr(t, 330 + 500 * t) * env(t, 0.12) * 0.25);
    B("pickupHealth", 0.3, (t) => {
      const f = t < 0.12 ? 520 : 780;
      return sin(t, f) * env(t % 0.14, 0.13) * 0.4;
    });
    B("pickupWeapon", 0.4, (t) => {
      const steps = [260, 330, 392, 523];
      const f = steps[Math.min(3, Math.floor(t / 0.1))];
      return (sqr(t, f) * 0.3 + sin(t, f * 2) * 0.2) * env(t % 0.11, 0.1) * 0.9;
    });
    B("pickupKey", 0.45, (t) => {
      const steps = [660, 880, 1320];
      const f = steps[Math.min(2, Math.floor(t / 0.15))];
      return sin(t, f) * env(t % 0.16, 0.15) * 0.45;
    });
    B("extraLife", 0.8, (t) => {
      const steps = [523, 659, 784, 1047, 1319];
      const f = steps[Math.min(4, Math.floor(t / 0.16))];
      return (sin(t, f) + sin(t, f * 2) * 0.3) * env(t % 0.17, 0.16) * 0.4;
    });

    // --- doors / exit ---
    B("door", 0.5, (t) => {
      const grind = noise() * 0.18;
      const hum = sin(t, 70 + 90 * t) * 0.3;
      const e = Math.sin(Math.PI * Math.min(1, t / 0.5));
      return (grind + hum) * e;
    });
    B("locked", 0.32, (t) => {
      const on = (t % 0.16) < 0.11;
      return on ? sqr(t, 130) * 0.35 : 0;
    });
    B("exitOpen", 0.9, (t) => {
      const steps = [392, 523, 659, 784];
      const f = steps[Math.min(3, Math.floor(t / 0.22))];
      return (sqr(t, f) * 0.25 + sin(t, f) * 0.35) * env(t % 0.23, 0.22) * 0.9;
    });

    // --- jingles ---
    B("levelDone", 1.4, (t) => {
      const steps = [523, 659, 784, 1047, 784, 1047];
      const i = Math.min(steps.length - 1, Math.floor(t / 0.22));
      const f = steps[i];
      return (sin(t, f) * 0.4 + sqr(t, f / 2) * 0.15) * env(t % 0.23, 0.22, 1.5);
    });
    B("gameOver", 1.6, (t) => {
      const steps = [440, 415, 392, 311];
      const i = Math.min(steps.length - 1, Math.floor(t / 0.4));
      const f = steps[i];
      return (saw(t, f) * 0.3 + sin(t, f / 2) * 0.3) * env(t % 0.41, 0.4, 1.2);
    });
    B("victory", 2.6, (t) => {
      const steps = [523, 523, 523, 659, 784, 659, 784, 1047, 1047];
      const i = Math.min(steps.length - 1, Math.floor(t / 0.28));
      const f = steps[i];
      return (sin(t, f) * 0.4 + sqr(t, f) * 0.12 + sin(t, f * 2) * 0.15) * env(t % 0.29, 0.28, 1.3);
    });
    B("blip", 0.07, (t) => sqr(t, 700) * env(t, 0.06, 4) * 0.25);
    B("start", 0.5, (t) => {
      const steps = [330, 440, 660];
      const f = steps[Math.min(2, Math.floor(t / 0.16))];
      return sqr(t, f) * env(t % 0.17, 0.16) * 0.3;
    });
  }

  play(name, { volume = 1, rate = 1, pan = 0 } = {}) {
    if (!this.ctx || !this.enabled) return;
    const buf = this.buffers[name];
    if (!buf) return;
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.playbackRate.value = rate;
    const gain = this.ctx.createGain();
    gain.gain.value = volume;
    src.connect(gain);
    if (this.ctx.createStereoPanner && pan !== 0) {
      const p = this.ctx.createStereoPanner();
      p.pan.value = Math.max(-1, Math.min(1, pan));
      gain.connect(p);
      p.connect(this.master);
    } else {
      gain.connect(this.master);
    }
    src.start();
  }

  // Positional world sound relative to the listener (player).
  playAt(name, x, y, listener, opts = {}) {
    if (!listener) return this.play(name, opts);
    const dx = x - listener.x;
    const dy = y - listener.y;
    const dist = Math.hypot(dx, dy);
    const volume = (opts.volume ?? 1) * Math.min(1, 3.5 / (1 + dist * 0.6));
    if (volume < 0.02) return;
    // project onto the player's right vector for stereo pan
    const rightX = -listener.dirY;
    const rightY = listener.dirX;
    const pan = dist > 0.001 ? ((dx * rightX + dy * rightY) / dist) * 0.7 : 0;
    this.play(name, { ...opts, volume, pan });
  }
}

export const audio = new AudioSys();
