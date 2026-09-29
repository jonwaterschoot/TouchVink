// State -> drawing: knob rotation, fader travel, pad glow, switch position,
// the user LED, and a dashed ring on a knob that is behind a pickup. Pure view
// code — never writes to the store.

import type { Panel } from './panel';
import type { DeviceStore, DeviceState } from '../core/state';

// Pointer at rest points to 7:30 (value 0); full travel is 300°.
const KNOB_SWEEP_DEG = 300;

export class PanelBindings {
  constructor(private panel: Panel, store: DeviceStore) {
    store.on((ev, s) => {
      if (ev.kind === 'state') this.sync(s, ev.prev);
    });
    this.sync(store.state, null);
  }

  private sync(s: DeviceState, prev: DeviceState | null) {
    for (let i = 0; i < 8; i++) {
      if (!prev || s.controls[i] !== prev.controls[i]) this.control(i, s.controls[i] / 127);
      const armed = ((s.pickup >> i) & 1) !== 0;
      this.panel.controlEl(i)?.classList.toggle('pickup-armed', armed);
    }
    if (!prev || s.pads !== prev.pads)
      this.panel.pads.forEach((el, i) => el.classList.toggle('active', ((s.pads >> i) & 1) !== 0));
    // What each switch's role is set to right now. That is the lever, unless a
    // CC has taken the role over (until the lever next moves). Positions are in
    // panel order, like the drawing's options.
    this.panel.sw1.dataset.pos = String(s.motion);
    this.panel.sw2.dataset.pos = String(s.noise);
    if (!prev || s.led !== prev.led) this.led(s.led / 127);
  }

  private control(i: number, v: number) {
    const knob = this.panel.knobs.get(i);
    if (knob) {
      knob.g.setAttribute('transform', `rotate(${(KNOB_SWEEP_DEG * v).toFixed(1)} ${knob.cx} ${knob.cy})`);
      return;
    }
    const fader = this.panel.faders.get(i);
    if (fader) fader.rect.setAttribute('y', (fader.maxY - v * (fader.maxY - fader.minY)).toFixed(2));
  }

  private led(v: number) {
    const rect = this.panel.ledUser;
    rect.style.opacity = (0.18 + 0.82 * v).toFixed(3);
    rect.style.filter = v > 0.02 ? `drop-shadow(0 0 ${(v * 5).toFixed(1)}px #ff4536)` : '';
  }
}
