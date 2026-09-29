// The MIDI drawer, after TouchPlaited's: a slider for every CC the firmware
// listens to, and a piano on ch 1 (a note sets the osc pitch and excites the
// loop). Everything goes through Input, so it plays the device when connected
// and the simulation when not — which is also the way to see a pickup: set a
// knob here, then drag that knob on the drawing through the value.

import { CCS, zoneOf, zoneValue as z, noteName } from '../core/controls-meta';
import type { DeviceStore, DeviceState } from '../core/state';
import type { Input } from '../core/input';
import { knobValue } from '../core/describe';

const WHITE_SEMIS = [0, 2, 4, 5, 7, 9, 11];
const BLACK_SEMIS: Record<number, number> = { 0: 1, 1: 3, 3: 6, 4: 8, 5: 10 };  // white idx → semi

/** A CC's current value, read back from the state. */
function ccValue(cc: number, s: DeviceState): number {
  if (cc >= 20 && cc <= 27) return s.controls[cc - 20];
  switch (cc) {
    case 28: return z(s.motion, 3);
    case 29: return z(s.noise, 3);
    case 30: return z(s.padMode, 2);
    case 31: return z(s.attackStep, 3);
    case 32: return z(s.decayStep, 3);
    case 33: return z(s.steerLatched ? 1 : 0, 2);
    case 34: return z(s.dist, 7);
    default: return 0;
  }
}

export class CcPanel {
  private drawer: HTMLDivElement;
  private baseOctave = 2;
  private pianoEl!: HTMLDivElement;
  private octLabel!: HTMLSpanElement;
  private sliders: { cc: number; input: HTMLInputElement; val: HTMLElement; show: (v: number) => string }[] = [];

  constructor(addToMenu: (el: HTMLElement) => void, private store: DeviceStore, private input: Input) {
    const btn = document.createElement('button');
    btn.textContent = 'MIDI panel';
    btn.className = 'menu-item';
    btn.onclick = () => this.toggle();
    addToMenu(btn);

    this.drawer = document.createElement('div');
    this.drawer.className = 'cc-drawer';
    const header = document.createElement('div');
    header.className = 'cc-header';
    const title = document.createElement('span');
    title.textContent = 'MIDI → TouchVink';
    const close = document.createElement('button');
    close.className = 'cc-close';
    close.textContent = '×';
    close.title = 'Close';
    close.onclick = () => this.drawer.classList.remove('open');
    header.append(title, close);
    this.drawer.append(header, this.buildCcList(), this.buildPianoSection());
    document.body.appendChild(this.drawer);
  }

  open() {
    this.refresh();
    this.drawer.classList.add('open');
  }

  private toggle() {
    if (this.drawer.classList.contains('open')) this.drawer.classList.remove('open');
    else this.open();
  }

  /** Sliders start where the instrument is, not at zero. */
  private refresh() {
    const s = this.store.state;
    for (const sl of this.sliders) {
      const v = ccValue(sl.cc, s);
      sl.input.value = String(v);
      sl.val.textContent = sl.show(v);
    }
  }

  private buildCcList(): HTMLElement {
    const wrap = document.createElement('div');
    wrap.className = 'cc-list';
    const title = document.createElement('div');
    title.className = 'cc-title';
    title.textContent = 'CC (any channel; the device sends on ch 1)';
    wrap.appendChild(title);

    for (const meta of CCS) {
      const row = document.createElement('label');
      row.className = 'cc-row';
      const show = meta.zones
        ? (v: number) => meta.zones![zoneOf(v, meta.zones!.length)]
        : (v: number) => knobValue(meta.cc - 20, v / 127);
      row.innerHTML =
        `<span class="cc-num">${meta.cc}</span>` +
        `<span class="cc-name">${meta.name}</span>` +
        '<span class="cc-val"></span>';
      row.title = `same as ${meta.shadows}`;
      const slider = document.createElement('input');
      slider.type = 'range';
      slider.min = '0';
      slider.max = '127';
      const val = row.querySelector<HTMLElement>('.cc-val')!;
      slider.oninput = () => {
        this.input.cc(meta.cc, Number(slider.value));
        val.textContent = show(Number(slider.value));
      };
      row.appendChild(slider);
      wrap.appendChild(row);
      this.sliders.push({ cc: meta.cc, input: slider, val, show });
    }
    this.refresh();
    return wrap;
  }

  private buildPianoSection(): HTMLElement {
    const wrap = document.createElement('div');
    wrap.className = 'piano-section';
    const bar = document.createElement('div');
    bar.className = 'piano-bar';
    const down = document.createElement('button');
    down.textContent = '−';
    const up = document.createElement('button');
    up.textContent = '+';
    this.octLabel = document.createElement('span');
    down.onclick = () => this.shiftOctave(-1);
    up.onclick = () => this.shiftOctave(1);
    bar.append('Osc pitch, ch 1 · oct ', down, this.octLabel, up);
    wrap.appendChild(bar);
    this.pianoEl = document.createElement('div');
    this.pianoEl.className = 'piano';
    this.pianoEl.addEventListener('contextmenu', (e) => e.preventDefault());
    wrap.appendChild(this.pianoEl);
    this.renderPiano();
    return wrap;
  }

  private shiftOctave(d: number) {
    this.baseOctave = Math.min(6, Math.max(0, this.baseOctave + d));
    this.renderPiano();
  }

  private renderPiano() {
    const octaves = 3;
    const base = (this.baseOctave + 1) * 12;  // MIDI: C-1 = 0
    this.octLabel.textContent = ` C${this.baseOctave}–C${this.baseOctave + octaves} `;
    this.pianoEl.innerHTML = '';
    const whites = octaves * 7 + 1;
    for (let w = 0; w < whites; w++) {
      const oct = Math.floor(w / 7);
      this.pianoEl.appendChild(this.key(base + oct * 12 + WHITE_SEMIS[w % 7], 'white', w, whites));
      const black = BLACK_SEMIS[w % 7];
      if (black !== undefined && w < whites - 1)
        this.pianoEl.appendChild(this.key(base + oct * 12 + black, 'black', w, whites));
    }
  }

  private key(note: number, color: 'white' | 'black', whiteIdx: number, whites: number): HTMLDivElement {
    const k = document.createElement('div');
    k.className = `piano-key ${color}`;
    const wPct = 100 / whites;
    if (color === 'white') {
      k.style.left = `${whiteIdx * wPct}%`;
      k.style.width = `${wPct}%`;
      if (note % 12 === 0) k.textContent = noteName(note);
    } else {
      k.style.left = `${(whiteIdx + 0.65) * wPct}%`;
      k.style.width = `${wPct * 0.7}%`;
    }
    k.title = `${noteName(note)} (${note})`;
    k.addEventListener('pointerdown', (e) => {
      k.setPointerCapture(e.pointerId);
      this.input.key(true, note);
      k.classList.add('down');
    });
    const off = (e: PointerEvent) => {
      if (!k.classList.contains('down')) return;
      k.releasePointerCapture?.(e.pointerId);
      this.input.key(false, note);
      k.classList.remove('down');
    };
    k.addEventListener('pointerup', off);
    k.addEventListener('pointercancel', off);
    return k;
  }
}
