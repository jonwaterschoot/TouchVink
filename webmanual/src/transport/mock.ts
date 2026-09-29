// Scripted demo: plays the panel through the local simulation — the same
// path your own clicks take — and invents a plausible loop meter, since the
// page runs no DSP. For rehearsing a video, or seeing the manual move.

import type { Transport } from './transport';
import type { DeviceStore } from '../core/state';
import type { LocalDevice } from '../core/sim';

const LOOP_S = 30;

type Step = [at: number, act: (sim: LocalDevice) => void];

// Pots as 7-bit values; pads as down/up pairs.
const tap = (pad: number, at: number, len = 0.25): Step[] => [
  [at, (s) => s.padDown(pad, 0.8)],
  [at + len, (s) => s.padUp(pad)],
];

const SCRIPT: Step[] = [
  // wake the loop: gain up, some ring, some reverb
  [0.5, (s) => s.setPot(4, 70)],
  [1.0, (s) => s.setPot(4, 90)],
  [1.5, (s) => s.setPot(4, 100)],
  [2.2, (s) => s.setPot(2, 50)],
  [2.8, (s) => s.setPot(3, 60)],
  // pitches
  ...tap(5, 3.6), ...tap(6, 4.4), ...tap(7, 5.2), ...tap(3, 6.0, 0.5),
  // delay sweep, from comb to echo
  ...[20, 35, 50, 70, 90, 110].map((v, k): Step => [7 + k * 0.35, (s) => s.setPot(5, v)]),
  // steering on, motion to drunk
  ...tap(1, 9.8, 0.15),
  [11.0, (s) => s.setSwitch(1, 2)],
  // distortion mode, pick fold then full-rect
  ...tap(2, 12.5), ...tap(6, 13.4, 0.6), ...tap(8, 14.6, 0.6),
  // decay to drone and back
  ...tap(11, 16.0, 0.15), ...tap(11, 16.8, 0.15),
  [18.0, (s) => s.setSwitch(2, 1)],
  ...tap(0, 19.0), ...tap(4, 19.8, 0.8),
  // a MIDI CC takes S34, then the pot is turned through it (pickup)
  [21.5, (s) => s.cc(24, 40)],
  ...[95, 80, 65, 50, 40, 36].map((v, k): Step => [22.5 + k * 0.3, (s) => s.setPot(4, v)]),
  // long-press P11: back to 120 ms
  ...tap(11, 25.0, 0.8),
  // steering off, motion steady, noise pink, pots home
  ...tap(1, 27.0, 0.15),
  [27.8, (s) => s.setSwitch(1, 1)],
  [28.2, (s) => s.setSwitch(2, 0)],
  [28.6, (s) => { s.setPot(2, 0); s.setPot(3, 25); s.setPot(5, 38); s.setPot(4, 64); }],
];

export class MockTransport implements Transport {
  readonly kind = 'mock';
  private timer: number | null = null;
  private t0 = 0;
  private next = 0;
  private steer = 0;

  constructor(private store: DeviceStore, private sim: LocalDevice) {}

  async connect(): Promise<void> {
    this.store.demo = true;
    this.t0 = performance.now();
    this.next = 0;
    this.timer = window.setInterval(() => this.tick(), 20);
  }

  disconnect(): void {
    if (this.timer !== null) clearInterval(this.timer);
    this.timer = null;
    this.sim.releaseAll();
    this.store.demo = false;
    this.store.setMeter({ ...this.store.meter, loopDb: 0 });
  }

  describe(): string {
    return 'Demo (scripted, simulated)';
  }

  private tick() {
    const t = ((performance.now() - this.t0) / 1000) % LOOP_S;
    if (t < 0.1 && this.next >= SCRIPT.length) this.next = 0;
    while (this.next < SCRIPT.length && SCRIPT[this.next][0] <= t) SCRIPT[this.next++][1](this.sim);

    // An invented loop: louder with S34, breathing, with steering now and then.
    const s = this.store.state;
    const gain = s.controls[4] / 127;
    const breathe = 0.5 + 0.5 * Math.sin(t * 1.7) * Math.sin(t * 0.43);
    const db = gain < 0.55 ? -60 + gain * 60 : -18 + breathe * 12;
    if (s.steerActive && Math.random() < 0.02) this.steer = (this.steer + 1) & 0x7f;
    const base = s.oscPad === 0x7f ? 440 * 2 ** ((s.midiNote - 69) / 12) : [0.8, 5, 27.5, 55, 110, 220, 440][s.oscPad];
    const wobble = s.motion === 0 ? 2 ** (0.25 * Math.sin(t * 1.07)) : s.motion === 2 ? 2 ** (0.3 * Math.sin(t * 3.1)) : 1;
    this.store.setMeter({
      ...this.store.meter,
      loopDb: Math.round(Math.max(0, Math.min(1, (db + 60) / 60)) * 127),
      steerCount: this.steer,
      oscHz: base * wobble,
    });
  }
}
