// The instrument's control logic, run in the page: what TouchVink.cpp does
// with pads, pots, switches and MIDI (ProcessPads, OnControlChange, the
// pickups, the LED blinks, the excitation envelope), without the DSP. It is
// what makes the manual playable with no device attached: tap a pad on the
// drawing and the screen, the LED and the info panel answer the way the
// panel would.
//
// Keep it in step with TouchVink.cpp. It only writes to the store while
// enabled: with a device connected, the device's telemetry owns the state.

import type { DeviceStore, DeviceState } from './state';
import {
  CC, CC_KNOB_BASE, CFG, MIDI_KEYS_CH, MIDI_PADS_CH, PAD_NOTE_BASE, zoneOf,
} from './controls-meta';

const P_OSC = 0, P_STEER = 1, P_DIST = 2, P_ATTACK = 10, P_DECAY = 11;
const STEER_HOLD_MS = 350;
const CALIB_HOLD_MS = 1000;
const HOLD_ANNOUNCE_MS = 150;
const PICKUP_NEAR = 0.015 * 127;  // config.h kPickupNear, in 7-bit steps
const ATTACK_S = [0.002, 0.06, 0.6];
const DECAY_S = [0.12, 2.0, -1];
const LED_STEP_MS = 90;           // TouchVink.cpp Led: 90 control ticks per half-blink

export class LocalDevice {
  enabled = true;
  private downAt = new Map<number, number>();
  private comboUsed = false;
  private calibStart = -1;
  private calibDone = false;
  private pickupValue = new Array(8).fill(0);
  private pickupBelow = new Array(8).fill(false);
  private motionCc = -1;
  private noiseCc = -1;
  private blinks = 0;
  private blinkAcc = 0;
  // excitation envelope, for the LED and the meter
  private env = 0;
  private envPeak = 0;
  private envStage: 'idle' | 'attack' | 'decay' | 'hold' = 'idle';
  private last = performance.now();

  constructor(private store: DeviceStore) {
    setInterval(() => this.tick(), 20);
  }

  private get s(): DeviceState {
    return this.store.state;
  }

  private apply(patch: Partial<DeviceState>) {
    if (this.enabled) this.store.apply(patch);
  }

  // ---------------------------------------------------------- pads

  padDown(i: number, velocity = 0.7) {
    if (!this.enabled || i < 0 || i > 11 || this.downAt.has(i)) return;
    this.downAt.set(i, performance.now());
    const pads = this.s.pads | (1 << i);
    this.apply({ pads });
    // P10 + P11 together: recalibrate after 1 s, and nothing else happens.
    if ((pads >> P_ATTACK) & 1 && (pads >> P_DECAY) & 1) {
      this.comboUsed = true;
      if (this.calibStart < 0) this.calibStart = performance.now();
      return;
    }
    if (i === P_OSC) { this.apply({ padMode: 0 }); this.blink(1); }
    if (i === P_DIST) { this.apply({ padMode: 1 }); this.blink(2); }
    if (i >= 3 && i <= 9) {
      if (this.s.padMode === 0) {
        this.apply({ oscPad: i - 3 });
        this.excite(velocity);
      } else {
        this.apply({ dist: i - 3 });
      }
    }
  }

  padUp(i: number) {
    if (!this.enabled || !this.downAt.has(i)) return;
    const held = performance.now() - this.downAt.get(i)!;
    this.downAt.delete(i);
    const pads = this.s.pads & ~(1 << i);
    this.apply({ pads });
    if (this.comboUsed) {
      // the releases that end a P10+P11 combo do nothing
      if (!((pads >> P_ATTACK) & 1) && !((pads >> P_DECAY) & 1)) this.comboUsed = false;
      if (i === P_ATTACK || i === P_DECAY) return;
    }
    if (i === P_STEER && held < STEER_HOLD_MS) {
      const on = !this.s.steerLatched;
      this.apply({ steerLatched: on });
      this.blink(on ? 3 : 1);
    }
    if (i === P_ATTACK) {
      const a = (this.s.attackStep + 1) % 3;
      this.apply({ attackStep: a });
      this.blink(a + 1);
    }
    if (i === P_DECAY) {
      const d = held >= CFG.longPressMs ? 0 : (this.s.decayStep + 1) % 3;
      this.apply({ decayStep: d });
      this.setDecayStage();
      this.blink(d + 1);
    }
    this.updateSteer();
  }

  releaseAll() {
    for (const i of [...this.downAt.keys()]) this.padUp(i);
  }

  // ---------------------------------------------------------- pots, switches

  /** A pot turned on the drawing, 0..127. Behind a pickup it drives nothing
   * until it reaches the CC value, or passes it. */
  setPot(i: number, v: number) {
    if (!this.enabled) return;
    const pots = [...this.s.pots];
    pots[i] = v;
    let pickup = this.s.pickup;
    if ((pickup >> i) & 1) {
      const target = this.pickupValue[i];
      if (Math.abs(v - target) < PICKUP_NEAR || (v < target) !== this.pickupBelow[i]) pickup &= ~(1 << i);
    }
    const controls = [...this.s.controls];
    controls[i] = (pickup >> i) & 1 ? this.pickupValue[i] : v;
    this.apply({ pots, controls, pickup });
  }

  /** Moving a lever hands its role back from MIDI. */
  setSwitch(which: 1 | 2, pos: number) {
    if (!this.enabled) return;
    if (which === 1) {
      this.motionCc = -1;
      this.apply({ sw1: pos, motion: pos });
    } else {
      this.noiseCc = -1;
      this.apply({ sw2: pos, noise: pos });
    }
  }

  // ---------------------------------------------------------- MIDI in

  cc(cc: number, v: number) {
    if (!this.enabled) return;
    if (cc >= CC_KNOB_BASE && cc < CC_KNOB_BASE + 8) {
      const i = cc - CC_KNOB_BASE;
      this.pickupValue[i] = v;
      this.pickupBelow[i] = this.s.pots[i] < v;
      const controls = [...this.s.controls];
      controls[i] = v;
      this.apply({ controls, pickup: this.s.pickup | (1 << i) });
      return;
    }
    switch (cc) {
      case CC.motion: this.motionCc = zoneOf(v, 3); this.apply({ motion: this.motionCc }); break;
      case CC.noise: this.noiseCc = zoneOf(v, 3); this.apply({ noise: this.noiseCc }); break;
      case CC.padMode: this.apply({ padMode: zoneOf(v, 2) }); break;
      case CC.attack: this.apply({ attackStep: zoneOf(v, 3) }); break;
      case CC.decay: this.apply({ decayStep: zoneOf(v, 3) }); this.setDecayStage(); break;
      case CC.steer: this.apply({ steerLatched: zoneOf(v, 2) === 1 }); this.updateSteer(); break;
      case CC.dist: this.apply({ dist: zoneOf(v, 7) }); break;
    }
  }

  noteOn(ch: number, note: number, vel: number) {
    if (ch === MIDI_PADS_CH) this.padDown(note - PAD_NOTE_BASE, vel / 127);
    else if (ch === MIDI_KEYS_CH && this.enabled) {
      this.apply({ oscPad: 0x7f, midiNote: note });
      this.excite(vel / 127);
    }
  }

  noteOff(ch: number, note: number) {
    if (ch === MIDI_PADS_CH) this.padUp(note - PAD_NOTE_BASE);
  }

  // ---------------------------------------------------------- time

  private tick() {
    const now = performance.now();
    const dt = now - this.last;
    this.last = now;
    if (!this.enabled) return;
    this.updateSteer();
    this.updateHold(now);
    this.updateEnvelope(dt / 1000);
    this.updateLed(dt);
  }

  private updateSteer() {
    const at = this.downAt.get(P_STEER);
    const momentary = at !== undefined && performance.now() - at >= STEER_HOLD_MS;
    this.apply({ steerActive: this.s.steerLatched || momentary });
  }

  private updateHold(now: number) {
    const both = this.downAt.has(P_ATTACK) && this.downAt.has(P_DECAY);
    if (!both) this.calibStart = -1;
    if (both && this.calibStart >= 0) {
      const t = now - this.calibStart;
      if (!this.calibDone && t >= CALIB_HOLD_MS) {
        this.calibDone = true;
        this.blink(5);
      }
      this.apply({ holdKind: 1, holdProgress: Math.round(Math.min(1, t / CALIB_HOLD_MS) * 127), holdStage: this.calibDone ? 1 : 0 });
      return;
    }
    this.calibDone = false;
    const at = this.downAt.get(P_DECAY);
    if (!this.comboUsed && at !== undefined && now - at >= HOLD_ANNOUNCE_MS) {
      const f = (now - at - HOLD_ANNOUNCE_MS) / (CFG.longPressMs - HOLD_ANNOUNCE_MS);
      this.apply({ holdKind: 2, holdProgress: Math.round(Math.min(1, f) * 127), holdStage: f >= 1 ? 1 : 0 });
      return;
    }
    this.apply({ holdKind: 0, holdProgress: 0, holdStage: 0 });
  }

  // ADEnv in dsp/blocks.h: linear attack, exponential decay, drone holds.
  private excite(velocity: number) {
    this.envPeak = 0.35 + 0.65 * velocity;
    this.envStage = 'attack';
  }

  private setDecayStage() {
    const drone = DECAY_S[this.s.decayStep] < 0;
    if (drone && this.envStage === 'decay') this.envStage = 'hold';
    if (!drone && this.envStage === 'hold') this.envStage = 'decay';
  }

  private updateEnvelope(dt: number) {
    const drone = DECAY_S[this.s.decayStep] < 0;
    switch (this.envStage) {
      case 'attack':
        this.env += dt / Math.max(ATTACK_S[this.s.attackStep], 0.0005);
        if (this.env >= this.envPeak) {
          this.env = this.envPeak;
          this.envStage = drone ? 'hold' : 'decay';
        }
        break;
      case 'decay':
        this.env *= Math.exp(-dt / (DECAY_S[this.s.decayStep] * 0.3));  // tau2coef(sec * 0.3)
        if (this.env < 0.0001) { this.env = 0; this.envStage = 'idle'; }
        break;
      case 'hold':
        this.env += (this.envPeak - this.env) * Math.min(1, dt * 48);
        break;
    }
    const m = this.store.meter;
    const excite = Math.round(this.env * 127);
    if (excite !== m.excite) this.store.setMeter({ ...m, excite });
  }

  // Led in TouchVink.cpp: n blinks of 90 ms off / 90 ms on, else the LED
  // follows the excitation (OSC mode) — the loop level in DIST mode is DSP
  // this page doesn't run, so it stays dark there.
  private blink(n: number) {
    this.blinks = n * 2;
    this.blinkAcc = 0;
  }

  private updateLed(dt: number) {
    let on: boolean;
    if (this.blinks > 0) {
      this.blinkAcc += dt;
      while (this.blinkAcc >= LED_STEP_MS && this.blinks > 0) {
        this.blinkAcc -= LED_STEP_MS;
        this.blinks--;
      }
      on = (this.blinks & 1) === 1;
    } else {
      on = this.s.padMode === 0 && this.env > 0.1;
    }
    this.apply({ led: on ? 127 : 0 });
  }

}
