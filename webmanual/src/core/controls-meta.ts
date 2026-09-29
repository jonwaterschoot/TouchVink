// Every control's name, role and manual text: the one source for the labels,
// the OLED texts, the knob map and the manual panel. Numbers marked "config.h"
// mirror common/config.h in the firmware; change them together.

export interface ControlMeta {
  svgId: string;
  name: string;   // silk-screen designator, "S31"
  fn: string;     // what it does, short: "Ext in"
  panel: string;  // the faceplate / sketch label
  doc: string;    // manual text
}

/** Index = S30..S37 (controls[] order). */
export const CONTROLS: ControlMeta[] = [
  {
    svgId: 'knob-s30', name: 'S30', fn: 'Osc/noise', panel: 'OSC / NOISE',
    doc: 'Internal source (IN2): the balance between the oscillator and the noise. ' +
      'It is used twice: free-running, it is the carrier the ring mod multiplies by; ' +
      'gated by the pad envelope, it is what the pads inject into the loop.',
  },
  {
    svgId: 'knob-s31', name: 'S31', fn: 'Ext in', panel: 'EXT',
    doc: 'External stereo input level (IN1), off to +12 dB on a squared taper. ' +
      'S36 decides how much of it excites the loop and carries the ring mod.',
  },
  {
    svgId: 'knob-s32', name: 'S32', fn: 'Ring mod', panel: 'RING',
    doc: "Vink's AC-MUP. Left, the loop passes straight through; right, it is multiplied " +
      'by the carrier (osc/noise or the external input, per S36). The excitation is added ' +
      'at every setting, so the pads always do something. Low osc pitches turn the ring ' +
      'mod into tremolo and chopping.',
  },
  {
    svgId: 'knob-s33', name: 'S33', fn: 'Reverb', panel: 'REV',
    doc: 'Reverb inside the loop, mix and decay together. Every pass goes through it, so ' +
      'the loop smears further each time round.',
  },
  {
    svgId: 'knob-s34', name: 'S34', fn: 'Loop gain', panel: 'VCA',
    doc: 'How much of the VCA output goes back into the delay. Below about 60 % the loop ' +
      'dies away like an echo. Above it the loop grows until its own loudness turns the ' +
      'VCA down: the Vink zone, where it keeps moving by itself. High, with no input, the ' +
      'loop blooms out of the tape-hiss floor within seconds.',
  },
  {
    svgId: 'knob-s35', name: 'S35', fn: 'Loop delay', panel: 'DELAY',
    doc: 'The loop delay (REC), 2 ms to 1.9 s on a log scale, with a tape-like pitch glide ' +
      'while you turn it. Short, the loop is a pitched comb (the screen names the pitch); ' +
      'long, a tape echo.',
  },
  {
    svgId: 'fader-s36', name: 'S36', fn: 'Source', panel: 'MIX SOURCE',
    doc: 'Source fader: internal source at the bottom, external input at the top. It sets ' +
      'both what excites the loop and what the ring mod multiplies by.',
  },
  {
    svgId: 'fader-s37', name: 'S37', fn: 'Output', panel: 'VOL OUT',
    doc: 'Output level. Only the output: the loop runs the same whatever this is set to.',
  },
];

/** P3..P9 in OSC mode: base pitch per pad, Hz (config.h kPadFreqs). */
export const PAD_FREQS = [0.8, 5, 27.5, 55, 110, 220, 440];
/** P3..P9 in DIST mode (dsp/blocks.h DistType). */
export const DIST_NAMES = ['clean', 'soft', 'hard', 'fold', 'half-rect', 'full-rect', 'crush'];
const DIST_DOCS = [
  'no distortion',
  'tanh saturation',
  'hard clip',
  'triangle wavefolder',
  'half-wave rectifier',
  'full-wave rectifier: an octave up',
  'sample-rate and bit crusher',
];

export interface PadMeta {
  svgId: string;
  name: string;
  short: string;  // static-label text
  doc: string;
}

function playPadDoc(i: number): string {
  const f = PAD_FREQS[i];
  const pitch = f < 20
    ? `${f} Hz, sub-audio: in the ring mod this becomes tremolo and chopping`
    : `${f} Hz`;
  return `OSC mode: sets the oscillator to ${pitch} and excites the loop; pressing harder ` +
    `bends up to an octave. DIST mode: ${DIST_NAMES[i]} (${DIST_DOCS[i]}); pressure adds drive.`;
}

/** Index = P0..P11 (pads[] order). */
export const PADS: PadMeta[] = [
  { svgId: 'pad-p0', name: 'P0', short: 'OSC',
    doc: 'Pads to OSC mode (the LED blinks once): P3 to P9 pick the oscillator pitch and excite the loop.' },
  { svgId: 'pad-p1fx', name: 'P1', short: 'Steer',
    doc: "Steering, Vink's T-SAH: when the loop swells above its own average, the osc pitch " +
      'is re-picked at random within an octave of the pad pitch, so the loop steers itself. ' +
      'Tap to latch on or off (3 blinks on, 1 off); hold for momentary.' },
  { svgId: 'pad-p2', name: 'P2', short: 'DIST',
    doc: 'Pads to DIST mode (the LED blinks twice): P3 to P9 pick the distortion inside the loop, ' +
      'after the VCA, so every pass adds harmonics and the compressor keeps them in check.' },
  ...PAD_FREQS.map((_, i) => ({
    svgId: `pad-p${i + 3}`, name: `P${i + 3}`, short: '', doc: playPadDoc(i),
  })),
  { svgId: 'pad-p10', name: 'P10', short: 'Attack',
    doc: 'Excitation attack, stepping 2 ms, 60 ms, 600 ms on each release. ' +
      'Hold P10 and P11 together for 1 s to recalibrate the pads (5 blinks).' },
  { svgId: 'pad-p11', name: 'P11', short: 'Decay',
    doc: 'Excitation decay, tap to step 120 ms, 2 s, drone. Hold 0.5 s to go back to 120 ms. ' +
      'Drone keeps the excitation open, and play-pad pressure freezes at its peak: pitch bend ' +
      'in OSC mode, drive in DIST mode.' },
];

export const SW1_NAMES = ['LFO', 'steady', 'drunk'];   // osc motion, left lever: left/mid/right
export const SW2_NAMES = ['pink', 'drift', 'brown'];   // noise colour, right lever: top/mid/bottom
export const SWITCHES = [
  { svgId: 'sw1', name: 'SW1', fn: 'Osc motion', names: SW1_NAMES,
    doc: 'How the oscillator moves by itself: a slow LFO sweep, steady, or a drunk walk. ' +
      'Both moving settings bend the pitch and the sine-to-saw shape.' },
  { svgId: 'sw2', name: 'SW2', fn: 'Noise', names: SW2_NAMES,
    doc: 'Noise colour: pink, pink drifting to brown and back on a slow LFO, or brown.' },
];

export const ATTACK_NAMES = ['2 ms', '60 ms', '600 ms'];
export const DECAY_NAMES = ['120 ms', '2 s', 'drone'];

// config.h
export const CFG = {
  minDelayMs: 2,
  maxDelayMs: 1900,
  maxLoopGain: 1.8,
  outMaxGain: 2,
  pressureBendOct: 1,
  longPressMs: 500,
};

// ------------------------------------------------------------ MIDI (config.h)
export const MIDI_KEYS_CH = 0;   // ch 1: notes set the osc pitch
export const MIDI_PADS_CH = 9;   // ch 10: note 36 + i = pad Pi
export const PAD_NOTE_BASE = 36;
export const CC_KNOB_BASE = 20;  // 20..27 = S30..S37

export interface CcMeta {
  cc: number;
  name: string;
  shadows: string;
  zones?: string[];  // a stepped setting: the 0..127 range split evenly
}

export const CCS: CcMeta[] = [
  ...CONTROLS.map((c, i) => ({ cc: CC_KNOB_BASE + i, name: c.fn, shadows: c.name })),
  { cc: 28, name: 'Osc motion', shadows: 'SW1', zones: SW1_NAMES },
  { cc: 29, name: 'Noise', shadows: 'SW2', zones: SW2_NAMES },
  { cc: 30, name: 'Pad mode', shadows: 'P0 / P2', zones: ['OSC', 'DIST'] },
  { cc: 31, name: 'Attack', shadows: 'P10', zones: ATTACK_NAMES },
  { cc: 32, name: 'Decay', shadows: 'P11', zones: DECAY_NAMES },
  { cc: 33, name: 'Steer', shadows: 'P1', zones: ['off', 'on'] },
  { cc: 34, name: 'Distortion', shadows: 'P3-P9 DIST', zones: DIST_NAMES },
];

export const CC = {
  motion: 28, noise: 29, padMode: 30, attack: 31, decay: 32, steer: 33, dist: 34,
} as const;

/** Decode a stepped CC the way the firmware does (config.h zone()). */
export function zoneOf(v: number, n: number): number {
  return Math.floor((v * n) / 128);
}

/** The value the firmware sends for zone z of n. */
export function zoneValue(z: number, n: number): number {
  return Math.floor((z * 127 + Math.floor((n - 1) / 2)) / (n - 1));
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export function noteName(n: number): string {
  return `${NOTE_NAMES[((n % 12) + 12) % 12]}${Math.floor(n / 12) - 1}`;
}
