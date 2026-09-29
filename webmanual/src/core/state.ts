// The device state, in the firmware's own wire units (0..127, step indexes):
// a field-for-field mirror of TelemetryState in midi/telemetry.h. Keeping the
// units means describe.ts and the OLED port run on exactly the numbers the
// panel's screen runs on.
//
// Written by the transports (MIDI telemetry, the demo) and, when nothing is
// connected, by the local simulation (sim.ts). Views subscribe and diff.

export interface DeviceState {
  pads: number;           // bit i = pad Pi touched
  controls: number[];     // S30..S37 in effect, 0..127
  pots: number[];         // S30..S37 physical pot, 0..127
  pickup: number;         // bit i = S3(0+i) behind a pickup (a CC set it)
  sw1: number;            // left lever: 0 left, 1 middle, 2 right
  sw2: number;            // right lever: 0 top, 1 middle, 2 bottom
  motion: number;         // in effect: 0 LFO, 1 steady, 2 drunk
  noise: number;          // in effect: 0 pink, 1 drift, 2 brown
  led: number;            // 0 or 127
  padMode: number;        // 0 OSC, 1 DIST
  steerLatched: boolean;
  steerActive: boolean;
  attackStep: number;     // 0..2
  decayStep: number;      // 0..2, 2 = drone
  oscPad: number;         // 0..6, 0x7f = the ch 1 note
  midiNote: number;
  dist: number;           // 0..6
  pitchPressure: number;  // 0..127
  distPressure: number;   // 0..127
  holdKind: number;       // 0 none, 1 recalibrate, 2 decay reset
  holdProgress: number;   // 0..127
  holdStage: number;      // 0 building, 1 fired
}

/** The METER frame: fast-moving, kept apart so it doesn't drive the views. */
export interface Meter {
  excite: number;      // 0..127
  loopDb: number;      // 0..127 = -60..0 dBFS
  steerCount: number;  // +1 per steering event, wraps at 128
  oscHz: number;
}

export type StateEvent =
  | { kind: 'state'; prev: DeviceState }
  | { kind: 'meter' }
  | { kind: 'connected'; v: boolean }
  | { kind: 'hello'; major: number; minor: number };

export type StateListener = (ev: StateEvent, s: DeviceState) => void;

export function initialState(): DeviceState {
  // The firmware's boot state (TouchVink.cpp UiState / engine Params).
  return {
    pads: 0,
    controls: [0, 64, 0, 25, 64, 38, 64, 76],
    pots: [0, 64, 0, 25, 64, 38, 64, 76],
    pickup: 0,
    sw1: 1,
    sw2: 0,
    motion: 1,
    noise: 0,
    led: 0,
    padMode: 0,
    steerLatched: false,
    steerActive: false,
    attackStep: 0,
    decayStep: 1,
    oscPad: 4,
    midiNote: 45,
    dist: 0,
    pitchPressure: 0,
    distPressure: 0,
    holdKind: 0,
    holdProgress: 0,
    holdStage: 0,
  };
}

function cloneState(s: DeviceState): DeviceState {
  return { ...s, controls: [...s.controls], pots: [...s.pots] };
}

function sameState(a: DeviceState, b: DeviceState): boolean {
  for (const k of Object.keys(a) as (keyof DeviceState)[]) {
    const x = a[k], y = b[k];
    if (Array.isArray(x)) {
      if ((x as number[]).some((v, i) => v !== (y as number[])[i])) return false;
    } else if (x !== y) return false;
  }
  return true;
}

export class DeviceStore {
  state: DeviceState = initialState();
  meter: Meter = { excite: 0, loopDb: 0, steerCount: 0, oscHz: 110 };
  connected = false;
  demo = false;  // the scripted demo is running (it invents a loop meter)
  firmware: string | null = null;
  private listeners: StateListener[] = [];

  on(fn: StateListener): () => void {
    this.listeners.push(fn);
    return () => {
      this.listeners = this.listeners.filter((l) => l !== fn);
    };
  }

  private emit(ev: StateEvent) {
    for (const l of this.listeners) l(ev, this.state);
  }

  /** Merge a change into the state; emits once, with the state before it. */
  apply(patch: Partial<DeviceState>) {
    const prev = this.state;
    const next = cloneState({ ...prev, ...patch });
    if (sameState(prev, next)) return;
    this.state = next;
    this.emit({ kind: 'state', prev });
  }

  setMeter(m: Meter) {
    this.meter = m;
    this.emit({ kind: 'meter' });
  }

  setConnected(v: boolean) {
    if (this.connected === v) return;
    this.connected = v;
    this.emit({ kind: 'connected', v });
  }

  setHello(major: number, minor: number) {
    this.firmware = `${major}.${minor}`;
    this.emit({ kind: 'hello', major, minor });
  }

  /** Back to the boot state (a transport disconnecting). */
  reset() {
    this.apply(initialState());
  }
}
