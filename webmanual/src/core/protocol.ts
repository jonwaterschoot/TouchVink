// TouchVink telemetry SysEx. Must stay in lockstep with midi/telemetry.cpp.
//
//   F0 7D 54 56 <ver> <type> <payload…> F7      "TV" = 0x54 0x56, mfr 0x7D
//
// STATE (0x01), 36 bytes, all 7-bit:
//   0-1   pads P0..P6 / P7..P11 bitmasks
//   2-9   S30..S37 value in effect
//   10-17 S30..S37 physical pot
//   18-19 pickup mask S30..S36 / S37
//   20 SW1 lever · 21 SW2 lever · 22 motion in effect · 23 noise in effect
//   24 LED · 25 flags: bit0 DIST mode, bit1 steer latched, bit2 steer active
//   26 attack step · 27 decay step · 28 osc pad (0x7F = ch 1 note) · 29 note
//   30 dist · 31 pitch pressure · 32 dist pressure
//   33 hold kind · 34 hold progress · 35 hold stage
// METER (0x06): excite · loop dB (0 = -60, 127 = 0 dBFS) · steer count ·
//   osc centi-Hz as 3 × 7 bits, low first
// HELLO (0x03): fw major · fw minor · feature bits
// REQUEST (0x7E), host → device, no payload: send HELLO + STATE + METER now.

import type { DeviceStore } from './state';

export const HEADER = [0x7d, 0x54, 0x56] as const;
export const PROTOCOL_VERSION = 1;

export const FrameType = {
  STATE: 0x01,
  HELLO: 0x03,
  METER: 0x06,
  REQUEST: 0x7e,
} as const;

export function isTelemetry(d: Uint8Array): boolean {
  return d.length >= 7 && d[0] === 0xf0 && d[1] === HEADER[0] && d[2] === HEADER[1] && d[3] === HEADER[2];
}

export function encodeRequest(): number[] {
  return [0xf0, ...HEADER, PROTOCOL_VERSION, FrameType.REQUEST, 0xf7];
}

/** Decode one SysEx message into the store. False if it wasn't ours. */
export function applySysex(data: Uint8Array, store: DeviceStore): boolean {
  if (!isTelemetry(data)) return false;
  const p = data.subarray(6, data.length - 1);
  switch (data[5]) {
    case FrameType.STATE: {
      if (p.length < 36) return true;
      store.apply({
        pads: p[0] | (p[1] << 7),
        controls: Array.from(p.subarray(2, 10)),
        pots: Array.from(p.subarray(10, 18)),
        pickup: p[18] | (p[19] << 7),
        sw1: p[20],
        sw2: p[21],
        motion: p[22],
        noise: p[23],
        led: p[24],
        padMode: p[25] & 1,
        steerLatched: (p[25] & 2) !== 0,
        steerActive: (p[25] & 4) !== 0,
        attackStep: p[26],
        decayStep: p[27],
        oscPad: p[28],
        midiNote: p[29],
        dist: p[30],
        pitchPressure: p[31],
        distPressure: p[32],
        holdKind: p[33],
        holdProgress: p[34],
        holdStage: p[35],
      });
      return true;
    }
    case FrameType.METER: {
      if (p.length < 6) return true;
      store.setMeter({
        excite: p[0],
        loopDb: p[1],
        steerCount: p[2],
        oscHz: (p[3] | (p[4] << 7) | (p[5] << 14)) / 100,
      });
      return true;
    }
    case FrameType.HELLO:
      if (p.length >= 2) store.setHello(p[0], p[1]);
      return true;
    default:
      return true;  // ours, but a frame type this page doesn't know
  }
}
