// The drawing as an instrument: press the pads, drag the knobs (up/down) and
// the faders, click a switch to step it. Everything goes through Input, so it
// plays the real device when connected and the simulation when not.
//
// Pointing at a control (hover, or the touch that plays it) also tells the
// manual panel which control to explain — see onFocus.

import type { Panel } from './panel';
import type { DeviceStore } from '../core/state';
import type { Input } from '../core/input';

export type Focus = { kind: 'control' | 'pad' | 'switch'; i: number };

const KNOB_DRAG_PX = 160;  // pointer travel for the full knob range

export function enableInteraction(
  panel: Panel, store: DeviceStore, input: Input, onFocus: (f: Focus) => void,
) {
  const svg = panel.svg;
  const ns = 'http://www.w3.org/2000/svg';
  // Touch screens: a long-press on the "device" must not open the context menu.
  svg.addEventListener('contextmenu', (e) => e.preventDefault());

  const hover = (el: Element, f: Focus) => {
    el.addEventListener('pointerenter', (e) => {
      if ((e as PointerEvent).pointerType === 'mouse') onFocus(f);
    });
  };

  // ---------------------------------------------------------- pads
  panel.pads.forEach((el, i) => {
    el.style.cursor = 'pointer';
    hover(el, { kind: 'pad', i });
    let down = false;
    el.addEventListener('pointerdown', (e) => {
      el.setPointerCapture(e.pointerId);
      down = true;
      onFocus({ kind: 'pad', i });
      // pen pressure is real velocity; mouse and touch get a medium hit
      const vel = e.pointerType === 'pen' && e.pressure > 0 ? Math.max(1, Math.round(e.pressure * 127)) : 90;
      input.padDown(i, vel);
      if (input.live) el.classList.add('midi-flash');
    });
    const up = (e: PointerEvent) => {
      if (!down) return;
      down = false;
      el.releasePointerCapture?.(e.pointerId);
      input.padUp(i);
      el.classList.remove('midi-flash');
    };
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', up);
  });

  // ---------------------------------------------------------- knobs
  for (const [i, knob] of panel.knobs) {
    // The artwork's knob is a stroked ring; give the whole cap a hit area.
    const hit = document.createElementNS(ns, 'circle');
    hit.setAttribute('cx', String(knob.cx));
    hit.setAttribute('cy', String(knob.cy));
    hit.setAttribute('r', '11');
    hit.setAttribute('class', 'hit');
    knob.g.appendChild(hit);
    knob.g.style.cursor = 'ns-resize';
    hover(knob.g, { kind: 'control', i });

    let start: { y: number; v: number } | null = null;
    knob.g.addEventListener('pointerdown', (e) => {
      knob.g.setPointerCapture(e.pointerId);
      // Simulated, the drag turns the pot, which drives nothing while a CC
      // holds the knob (the drawing shows the value in effect, so it stays
      // put until the pot picks up, as on the panel). Over MIDI the drag is
      // the CC, so it moves the value in effect.
      start = { y: e.clientY, v: input.live ? store.state.controls[i] : store.state.pots[i] };
      onFocus({ kind: 'control', i });
    });
    knob.g.addEventListener('pointermove', (e) => {
      if (!start) return;
      const v = Math.round(Math.min(127, Math.max(0, start.v + ((start.y - e.clientY) / KNOB_DRAG_PX) * 127)));
      if (v !== store.state.pots[i] || input.live) input.pot(i, v);
    });
    const end = () => { start = null; };
    knob.g.addEventListener('pointerup', end);
    knob.g.addEventListener('pointercancel', end);
  }

  // ---------------------------------------------------------- faders
  for (const [i, fader] of panel.faders) {
    const el = fader.rect;
    el.style.cursor = 'ns-resize';
    hover(el, { kind: 'control', i });
    let grabbed = false;
    const toValue = (clientY: number) => {
      // pointer y -> SVG user units, then along the handle's travel
      const m = svg.getScreenCTM();
      if (!m) return null;
      const y = (clientY - m.f) / m.d - el.height.baseVal.value / 2;
      const v = (fader.maxY - y) / (fader.maxY - fader.minY);
      return Math.round(Math.min(1, Math.max(0, v)) * 127);
    };
    el.addEventListener('pointerdown', (e) => {
      el.setPointerCapture(e.pointerId);
      grabbed = true;
      onFocus({ kind: 'control', i });
    });
    el.addEventListener('pointermove', (e) => {
      if (!grabbed) return;
      const v = toValue(e.clientY);
      if (v !== null) input.pot(i, v);
    });
    const end = () => { grabbed = false; };
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
  }

  // ---------------------------------------------------------- switches
  ([[panel.sw1, 1], [panel.sw2, 2]] as const).forEach(([el, which]) => {
    el.style.cursor = 'pointer';
    hover(el, { kind: 'switch', i: which - 1 });
    el.addEventListener('pointerdown', (e) => e.stopPropagation());
    el.addEventListener('click', () => {
      onFocus({ kind: 'switch', i: which - 1 });
      const s = store.state;
      input.switchTo(which, ((which === 1 ? s.motion : s.noise) + 1) % 3);
    });
  });
}
