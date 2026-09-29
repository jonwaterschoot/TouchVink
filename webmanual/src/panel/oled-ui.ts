// What the panel's screen shows and when: a line-for-line port of
// display/oled_ui.cpp (OledUi::Service / Draw), run on the same state fields
// with the same priority, throttle and idle timing. Called on a fast tick with
// the current state, exactly like the firmware's main loop calls it.

import type { DeviceState } from '../core/state';
import type { OledMini } from './oled-mini';
import {
  knobLabel, knobValue, padText, pitchValue, distValue, statusText,
  holdLabel, holdNote, holdConfirm, motionName, noiseName,
} from '../core/describe';
import { ATTACK_NAMES, DECAY_NAMES } from '../core/controls-meta';

const IDLE_MS = 2200;
const MIN_REDRAW_MS = 40;
const CONFIRM_FLASH_MS = 400;
const PRESSURE_STEP = 3;

const v01 = (v7: number) => (v7 & 0x7f) / 127;

/** A callout the screen just drew: `key` names the control, for the log. */
export type CalloutListener = (key: string, label: string, value: string) => void;

export class OledUi {
  private last: DeviceState | null = null;
  private nextDraw = 0;
  private idleAt = 0;
  private showingStatus = false;
  private padCallout = -1;
  private statusLabel = '';
  private statusValue = '';
  private statusSteer = false;

  constructor(private oled: OledMini, private onCallout: CalloutListener) {}

  service(t: DeviceState, now: number) {
    if (this.last === null) {
      // The boot line is up: it gets the full idle time, like on the panel.
      this.last = t;
      this.idleAt = now;
      return;
    }
    if (now < this.nextDraw) return;
    this.draw(t, now);
  }

  private draw(t: DeviceState, now: number) {
    const last = this.last!;
    const steer = t.steerActive;

    // 1. A hold toward a threshold owns the screen.
    if (t.holdKind !== 0) {
      const confirmed = t.holdStage !== 0 && (t.holdKind !== last.holdKind || t.holdStage !== last.holdStage);
      if (confirmed) {
        this.oled.showLine(holdLabel(t.holdKind), holdConfirm(t.holdKind));
        this.onCallout('hold', holdLabel(t.holdKind), holdConfirm(t.holdKind));
        this.nextDraw = now + CONFIRM_FLASH_MS;
      } else if (t.holdStage === 0) {
        this.oled.showProgress(holdLabel(t.holdKind), t.holdProgress, holdNote(t.holdKind));
        this.nextDraw = now + MIN_REDRAW_MS;
      }
      this.last = t;
      this.idleAt = now;
      this.showingStatus = false;
      this.padCallout = -1;
      return;
    }
    const holdEnded = last.holdKind !== 0;

    let key = '';
    let label = '';
    let value = '';
    let pickupKnob = -1;
    const newTouches = t.pads & ~last.pads;

    if (newTouches !== 0) {
      let i = 0;
      while (!((newTouches >> i) & 1)) i++;
      ({ label, value } = padText(i, t));
      key = `P${i}`;
      this.padCallout = i >= 3 && i <= 9 ? i : -1;
    } else if (t.steerLatched !== last.steerLatched) {
      key = 'steer'; label = 'P1 Steer'; value = t.steerLatched ? 'latched on' : 'off';
    } else if (t.steerActive !== last.steerActive) {
      key = 'steer'; label = 'P1 Steer'; value = t.steerActive ? 'momentary' : 'off';
    } else if (t.padMode !== last.padMode) {
      key = 'mode'; label = 'Pad mode'; value = t.padMode ? 'DIST' : 'OSC';
    } else if (t.attackStep !== last.attackStep) {
      key = 'P10'; label = 'P10 Attack'; value = ATTACK_NAMES[t.attackStep % 3];
    } else if (t.decayStep !== last.decayStep) {
      key = 'P11'; label = 'P11 Decay'; value = DECAY_NAMES[t.decayStep % 3];
    } else if (t.oscPad !== last.oscPad || t.midiNote !== last.midiNote) {
      key = 'pitch'; label = t.oscPad === 0x7f ? 'MIDI note' : 'Osc pitch'; value = pitchValue(t);
    } else if (t.dist !== last.dist) {
      key = 'dist'; label = 'Distortion'; value = distValue(t);
    } else if (t.motion !== last.motion || t.sw1 !== last.sw1) {
      key = 'SW1'; label = 'SW1 Osc motion'; value = motionName(t);
    } else if (t.noise !== last.noise || t.sw2 !== last.sw2) {
      key = 'SW2'; label = 'SW2 Noise'; value = noiseName(t);
    } else {
      for (let i = 0; i < 8; i++) {
        const armed = ((t.pickup >> i) & 1) !== 0;
        if (t.controls[i] !== last.controls[i]) {
          // moved while armed = MIDI moved it (the pot isn't connected)
          label = knobLabel(i, armed);
        } else if (armed && t.pots[i] !== last.pots[i]) {
          label = knobLabel(i, false);
          pickupKnob = i;
        } else if (!armed && ((last.pickup >> i) & 1)) {
          label = knobLabel(i, false);  // just picked up: the track goes
        } else continue;
        value = knobValue(i, v01(t.controls[i]));
        key = `S${30 + i}`;
        break;
      }
    }
    if (this.padCallout >= 0 && !((t.pads >> this.padCallout) & 1)) this.padCallout = -1;

    // A held play pad's callout follows its pressure, in steps.
    let keepPressure = false;
    if (!key && this.padCallout >= 0) {
      const pr = t.padMode ? t.distPressure : t.pitchPressure;
      const was = t.padMode ? last.distPressure : last.pitchPressure;
      if (Math.abs(pr - was) >= PRESSURE_STEP || (pr === 0 && was !== 0)) {
        ({ label, value } = padText(this.padCallout, t));
        key = `P${this.padCallout}`;
      } else keepPressure = true;
    }

    if (key) {
      if (pickupKnob >= 0) this.oled.showPickup(label, value, t.pots[pickupKnob], t.controls[pickupKnob]);
      else this.oled.showLine(label, value, steer);
      this.onCallout(key, label, value);
      this.nextDraw = now + MIN_REDRAW_MS;
      this.idleAt = now;
      this.showingStatus = false;
      this.last = t;
      return;
    }

    // Nothing new: the status row once the last callout had its time.
    if (this.padCallout >= 0) this.idleAt = now;
    const st = statusText(t);
    const idle = holdEnded || now - this.idleAt >= IDLE_MS;
    const changed = st.label !== this.statusLabel || st.value !== this.statusValue || steer !== this.statusSteer;
    if (idle && (!this.showingStatus || changed)) {
      this.oled.showLine(st.label, st.value, steer);
      this.nextDraw = now + MIN_REDRAW_MS;
      this.showingStatus = true;
      this.statusLabel = st.label;
      this.statusValue = st.value;
      this.statusSteer = steer;
    }
    this.last = keepPressure
      ? { ...t, pitchPressure: last.pitchPressure, distPressure: last.distPressure }
      : t;
  }
}
