// Where a gesture on the page goes. With the device connected over MIDI it is
// sent as MIDI (pads as ch 10 notes, pots and switches as CCs, the piano on
// ch 1) and the device's telemetry brings the result back. With nothing
// connected, the same gesture drives the local simulation (sim.ts), so the
// manual answers exactly as the panel would.

import { midiOut } from '../transport/output';
import type { LocalDevice } from './sim';
import {
  CC, CC_KNOB_BASE, MIDI_KEYS_CH, MIDI_PADS_CH, PAD_NOTE_BASE, zoneValue,
} from './controls-meta';

export class Input {
  constructor(private sim: LocalDevice) {}

  /** True when gestures go to the real device. */
  get live(): boolean {
    return midiOut.available;
  }

  padDown(i: number, velocity = 90) {
    if (this.live) midiOut.send([0x90 | MIDI_PADS_CH, PAD_NOTE_BASE + i, velocity]);
    else this.sim.padDown(i, velocity / 127);
  }

  padUp(i: number) {
    if (this.live) midiOut.send([0x80 | MIDI_PADS_CH, PAD_NOTE_BASE + i, 0]);
    else this.sim.padUp(i);
  }

  /** A pot or fader, 0..127. Over MIDI this is a CC, which the device treats
   * as MIDI taking the knob over until its pot is turned through the value. */
  pot(i: number, v: number) {
    if (this.live) midiOut.send([0xb0 | MIDI_KEYS_CH, CC_KNOB_BASE + i, v]);
    else this.sim.setPot(i, v);
  }

  switchTo(which: 1 | 2, pos: number) {
    if (this.live) midiOut.send([0xb0 | MIDI_KEYS_CH, which === 1 ? CC.motion : CC.noise, zoneValue(pos, 3)]);
    else this.sim.setSwitch(which, pos);
  }

  cc(cc: number, v: number) {
    if (this.live) midiOut.send([0xb0 | MIDI_KEYS_CH, cc, v]);
    else this.sim.cc(cc, v);
  }

  key(on: boolean, note: number, velocity = 100) {
    if (this.live) midiOut.send(on ? [0x90 | MIDI_KEYS_CH, note, velocity] : [0x80 | MIDI_KEYS_CH, note, 0]);
    else if (on) this.sim.noteOn(MIDI_KEYS_CH, note, velocity);
    else this.sim.noteOff(MIDI_KEYS_CH, note);
  }
}
