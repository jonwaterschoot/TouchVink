// What every control says, from the state's wire fields. A mirror of the
// formatters in display/oled_ui.cpp: same labels, same units, same rounding,
// so the page and the panel's screen print the same characters. Plain ASCII,
// like the firmware's Font_6x8.

import type { DeviceState } from './state';
import {
  CONTROLS, CFG, CC_KNOB_BASE, PAD_FREQS, DIST_NAMES, SW1_NAMES, SW2_NAMES,
  ATTACK_NAMES, DECAY_NAMES, noteName,
} from './controls-meta';

const v01 = (v7: number) => (v7 & 0x7f) / 127;

/** Fixed decimals, rounded half up: append_fixed() in oled_ui.cpp. */
export function fixed(v: number, decimals: number): string {
  const neg = v < 0;
  const scale = 10 ** decimals;
  const n = Math.floor(Math.abs(v) * scale + 0.5);
  const int = Math.floor(n / scale);
  const frac = decimals > 0 ? `.${String(n % scale).padStart(decimals, '0')}` : '';
  return `${neg ? '-' : ''}${int}${frac}`;
}

export const pct = (v: number) => `${Math.floor(v * 100 + 0.5)}%`;

export function db(gain: number): string {
  if (gain < 0.0001) return 'off';
  const d = 20 * Math.log10(gain);
  return `${d >= 0 ? '+' : ''}${fixed(d, 1)} dB`;
}

export const hz = (f: number) => `${fixed(f, f < 100 ? 1 : 0)} Hz`;

export function ms(t: number): string {
  if (t < 10) return `${fixed(t, 1)} ms`;
  if (t < 1000) return `${fixed(t, 0)} ms`;
  return `${fixed(t * 0.001, 2)} s`;
}

export const loopGain = (v: number) => CFG.maxLoopGain * Math.pow(v, 1.2);
export const delayMs = (v: number) => CFG.minDelayMs * Math.pow(CFG.maxDelayMs / CFG.minDelayMs, v);

/** A knob's value in the units the engine uses it in. `v` is 0..1. */
export function knobValue(i: number, v: number): string {
  switch (i) {
    case 0: return v <= 0.01 ? 'osc only' : v >= 0.99 ? 'noise only' : `noise ${pct(v)}`;
    case 1: return db(v * v * 4);
    case 2: return v <= 0.01 ? 'add only' : v >= 0.99 ? 'ring only' : `ring ${pct(v)}`;
    case 3: return v <= 0.01 ? 'dry' : pct(v);
    case 4: {
      const g = loopGain(v);
      return `x${fixed(g, 2)} ${g < 1 ? 'decays' : 'grows'}`;
    }
    case 5: {
      const t = delayMs(v);
      return t < 20 ? `${ms(t)} ${hz(1000 / t)}` : ms(t);
    }
    case 6: return v <= 0.01 ? 'internal' : v >= 0.99 ? 'external' : `ext ${pct(v)}`;
    case 7: return db(v * v * CFG.outMaxGain);
    default: return '';
  }
}

export function knobLabel(i: number, viaMidi: boolean): string {
  return `${viaMidi ? `CC${CC_KNOB_BASE + i}` : CONTROLS[i].name} ${CONTROLS[i].fn}`;
}

export function baseHz(s: DeviceState): number {
  if (s.oscPad === 0x7f) return 440 * Math.pow(2, (s.midiNote - 69) / 12);
  return PAD_FREQS[s.oscPad < 7 ? s.oscPad : 4];
}

export function pitchValue(s: DeviceState): string {
  const f = baseHz(s) * Math.pow(2, v01(s.pitchPressure) * CFG.pressureBendOct);
  return `${s.oscPad === 0x7f ? `${noteName(s.midiNote)} ` : ''}${hz(f)}`;
}

export function distValue(s: DeviceState): string {
  const name = DIST_NAMES[s.dist < 7 ? s.dist : 0];
  return s.distPressure > 2 ? `${name} +${pct(v01(s.distPressure))}` : name;
}

export function padText(i: number, s: DeviceState): { label: string; value: string } {
  const p = `P${i} `;
  if (i === 0 || i === 2) return { label: `${p}Pad mode`, value: s.padMode ? 'DIST' : 'OSC' };
  if (i === 1) return { label: `${p}Steer`, value: s.steerLatched ? 'on - tap off' : 'tap/hold' };
  if (i === 10) return { label: `${p}Attack`, value: ATTACK_NAMES[s.attackStep % 3] };
  if (i === 11) return { label: `${p}Decay`, value: DECAY_NAMES[s.decayStep % 3] };
  if (s.padMode === 0) return { label: `${p}Osc pitch`, value: pitchValue(s) };
  return { label: `${p}Distortion`, value: distValue(s) };
}

export function statusText(s: DeviceState): { label: string; value: string } {
  return {
    label: `${s.padMode ? 'DIST' : 'OSC'} A ${ATTACK_NAMES[s.attackStep % 3]} D ${DECAY_NAMES[s.decayStep % 3]}`,
    value: s.padMode ? distValue(s) : pitchValue(s),
  };
}

export const holdLabel = (kind: number) => (kind === 1 ? 'P10+P11 Recalibrate' : 'P11 Decay reset');
export const holdNote = (kind: number) => (kind === 1 ? 'keep both held' : 'back to 120 ms');
export const holdConfirm = (kind: number) => (kind === 1 ? 'pads done' : '120 ms');

export const motionName = (s: DeviceState) => SW1_NAMES[s.motion % 3];
export const noiseName = (s: DeviceState) => SW2_NAMES[s.noise % 3];

/** What a play pad (P3..P9) does in the current pad mode, for static labels. */
export function playPadShort(i: number, s: DeviceState): string {
  const k = i - 3;
  return s.padMode ? DIST_NAMES[k] : hz(PAD_FREQS[k]);
}
