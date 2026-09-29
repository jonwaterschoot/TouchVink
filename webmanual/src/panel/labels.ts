// Everything drawn over the panel, after TouchPlaited's labels.ts:
//  - the 128×32 screen (oled-mini.ts), driven by the port of the firmware's
//    OledUi (oled-ui.ts), and the optional wider screen above the Daisy
//  - the glow on whatever just changed
//  - the draggable info panel: status, the live meter, the manual entry for
//    the control you point at, the knob map, and the last few actions
//  - the static label overlays (none / designators / full labels)

import type { Panel } from './panel';
import type { Focus } from './interact';
import { OledWide } from './oled-wide';
import { OledMini } from './oled-mini';
import { OledUi } from './oled-ui';
import { svgToOverlay as mapSvgToOverlay, labelScale, LABEL_SCALE_EVENT } from './overlay-utils';
import type { DeviceStore, DeviceState } from '../core/state';
import { SettingsBar } from '../ui/settings-bar';
import type { MenuSection } from '../ui/toolbar';
import { CONTROLS, PADS, SWITCHES, ATTACK_NAMES, DECAY_NAMES, CC_KNOB_BASE } from '../core/controls-meta';
import {
  knobValue, pitchValue, distValue, statusText, motionName, noiseName, playPadShort, hz, padText,
} from '../core/describe';

const HL_TTL_MS = 1600;
const INFO_POS_KEY = 'tv-info-pos';
const INFO_SCALE_KEY = 'tv-info-scale';
const INFO_SIZE_KEY = 'tv-info-size';
const MAP_OPEN_KEY = 'tv-map-open';
const OVERLAY_MODE_KEY = 'tv-overlay-mode';
const OLED_WIDE_VISIBLE_KEY = 'tv-oled-wide-visible';

type OverlayMode = 'dynamic' | 'ids' | 'full';
const OVERLAY_MODES: OverlayMode[] = ['dynamic', 'ids', 'full'];
const OVERLAY_MODE_LABEL: Record<OverlayMode, string> = { dynamic: 'dyn', ids: 'S#', full: 'Aa' };

// Knob radius incl. ring stroke, SVG units — label anchors are computed from
// the knob centre, since the rendered bbox wobbles with the pointer's rotation.
const KNOB_R = 11;
// The screen: the faceplate zone between the knob columns, below S31–S34.
const MINI_SCREEN = { x: 66.1, y: 148, w: 100.1, h: 44 };

const INTRO =
  'A delayed copy of the output is ring-modulated, reverberated and sent through a VCA that ' +
  "the loop's own loudness turns down. That compressor lets the loop run above unity gain " +
  'without blowing up, so it keeps moving by itself. After the patches of Jaap Vink ' +
  '(Institute of Sonology).';
const HOWTO =
  'Point at any control to read what it does. Press the pads, drag the knobs and faders, ' +
  'click the switches: with nothing connected the page plays the panel\'s logic itself, and ' +
  'the screen answers as the real one would. Connect MIDI (menu) to mirror and play the device.';

const esc = (s: string) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');

/** "S34 Loop gain" + "x1.26 grows" -> log html, first word bold. */
function lineHtml(label: string, value: string): string {
  const sp = label.indexOf(' ');
  const head = sp > 0 ? `<b>${esc(label.slice(0, sp))}</b>${esc(label.slice(sp))}` : `<b>${esc(label)}</b>`;
  return value ? `${head} <span>${esc(value)}</span>` : head;
}

export class Labels {
  private oledMini: OledMini;
  private oledWide: OledWide;
  private oledUi: OledUi | null = null;
  private hls = new Map<Element, number>();
  private infoPanel: HTMLDivElement;
  private status: HTMLDivElement;
  private meterEl: HTMLDivElement;
  private manualEl: HTMLDivElement;
  private mapHead: HTMLDivElement;
  private mapBody: HTMLDivElement;
  private mapOpen: boolean;
  private logEl: HTMLDivElement;
  private log: { key: string; html: string }[] = [];
  private staticWrap: HTMLDivElement;
  private overlayMode: OverlayMode = 'dynamic';
  private focus: Focus | null = null;
  private hlTarget: Element | null = null;
  private lastSteerCount = -1;
  private steerFlashAt = 0;
  private infoScale = 1;

  constructor(
    private overlay: HTMLElement,
    private panel: Panel,
    private store: DeviceStore,
    settings: SettingsBar,
    addToMenu: (el: HTMLElement, section?: MenuSection) => void,
  ) {
    this.staticWrap = document.createElement('div');
    this.staticWrap.className = 'static-labels';
    overlay.appendChild(this.staticWrap);

    this.oledMini = new OledMini(overlay, panel, MINI_SCREEN);
    this.oledWide = new OledWide(overlay, panel);
    const wideBtn = document.createElement('button');
    wideBtn.className = 'menu-item';
    const wideLabel = (v: boolean) => `Expanded display: ${v ? 'On' : 'Off'}`;
    wideBtn.textContent = wideLabel(this.oledWide.isVisible());
    wideBtn.title = 'Show/hide the wider screen above the Daisy (history + status)';
    wideBtn.addEventListener('click', () => {
      const v = !this.oledWide.isVisible();
      this.oledWide.setVisible(v);
      localStorage.setItem(OLED_WIDE_VISIBLE_KEY, v ? '1' : '0');
      wideBtn.textContent = wideLabel(v);
    });
    addToMenu(wideBtn, 'view');

    // --- info panel
    this.infoPanel = document.createElement('div');
    this.infoPanel.className = 'info-panel';
    const handle = document.createElement('div');
    handle.className = 'info-handle';
    const dragIcon = document.createElement('span');
    dragIcon.className = 'info-drag-icon';
    dragIcon.textContent = '⠿ TouchVink';
    handle.append(dragIcon);
    this.status = document.createElement('div');
    this.status.className = 'status-chip';
    this.meterEl = document.createElement('div');
    this.meterEl.className = 'meter';
    this.manualEl = document.createElement('div');
    this.manualEl.className = 'manual';
    const mapSection = document.createElement('div');
    mapSection.className = 'model-section';
    this.mapHead = document.createElement('div');
    this.mapHead.className = 'model-head';
    this.mapBody = document.createElement('div');
    this.mapBody.className = 'model-body';
    mapSection.append(this.mapHead, this.mapBody);
    const storedOpen = localStorage.getItem(MAP_OPEN_KEY);
    this.mapOpen = storedOpen !== null ? storedOpen === '1' : !matchMedia('(max-width: 820px), (max-height: 500px)').matches;
    // stopPropagation: the panel-drag pointerdown captures the pointer and
    // would swallow these clicks
    this.mapHead.addEventListener('pointerdown', (e) => e.stopPropagation());
    this.mapHead.addEventListener('click', () => {
      this.mapOpen = !this.mapOpen;
      localStorage.setItem(MAP_OPEN_KEY, this.mapOpen ? '1' : '0');
      this.renderMap(store.state);
    });
    this.mapBody.addEventListener('pointerdown', (e) => e.stopPropagation());
    this.mapBody.addEventListener('mouseover', (e) => {
      this.setHl((e.target as HTMLElement).closest<HTMLElement>('[data-ctl],[data-pad],[data-sw]'));
    });
    this.mapBody.addEventListener('mouseleave', () => this.setHl(null));
    this.mapBody.addEventListener('click', (e) => {
      const row = (e.target as HTMLElement).closest<HTMLElement>('[data-ctl],[data-pad],[data-sw]');
      if (!row) return;
      if (row.dataset.ctl !== undefined) this.setFocus({ kind: 'control', i: +row.dataset.ctl });
      else if (row.dataset.pad !== undefined) this.setFocus({ kind: 'pad', i: +row.dataset.pad });
      else this.setFocus({ kind: 'switch', i: +row.dataset.sw! });
    });
    this.manualEl.addEventListener('pointerdown', (e) => e.stopPropagation());
    this.manualEl.addEventListener('click', (e) => {
      if ((e.target as HTMLElement).closest('.manual-back')) this.setFocus(null);
    });
    this.logEl = document.createElement('div');
    this.logEl.className = 'action-log';
    const grip = document.createElement('div');
    grip.className = 'info-grip';
    grip.textContent = '◢';
    const scroll = document.createElement('div');
    scroll.className = 'info-scroll';
    scroll.append(this.status, this.meterEl, this.manualEl, mapSection, this.logEl);
    this.infoPanel.append(handle, scroll, grip);
    overlay.appendChild(this.infoPanel);

    // --- settings bar entries
    const storedMode = localStorage.getItem(OVERLAY_MODE_KEY) as OverlayMode | null;
    if (storedMode && OVERLAY_MODES.includes(storedMode)) this.overlayMode = storedMode;
    const ovBtn = SettingsBar.button(OVERLAY_MODE_LABEL[this.overlayMode],
      'Label overlay: screen only / designators / full labels', () => {
        this.overlayMode = OVERLAY_MODES[(OVERLAY_MODES.indexOf(this.overlayMode) + 1) % OVERLAY_MODES.length];
        localStorage.setItem(OVERLAY_MODE_KEY, this.overlayMode);
        ovBtn.textContent = OVERLAY_MODE_LABEL[this.overlayMode];
        this.renderStatic(this.store.state);
      });
    settings.addGroup('Labels', 10, ovBtn);
    const mkFont = (txt: string, title: string, d: number) =>
      SettingsBar.button(txt, title, () => {
        this.applyInfoScale(this.infoScale + d);
        localStorage.setItem(INFO_SCALE_KEY, String(this.infoScale));
      });
    settings.addGroup('Info', 30,
      mkFont('A−', 'Smaller info panel text', -0.15),
      mkFont('A+', 'Larger info panel text', 0.15));
    settings.onReset(() => {
      localStorage.removeItem(INFO_POS_KEY);
      localStorage.removeItem(INFO_SCALE_KEY);
      localStorage.removeItem(INFO_SIZE_KEY);
      this.applyInfoScale(1);
      this.applyInfoSize(null, null);
      this.placeInfoPanel();
    });

    this.initInfoDrag(handle);
    this.initInfoResize(grip);
    this.applyInfoScale(parseFloat(localStorage.getItem(INFO_SCALE_KEY) ?? '1'));
    try {
      const sz = JSON.parse(localStorage.getItem(INFO_SIZE_KEY) ?? 'null');
      if (sz) this.applyInfoSize(sz.w, sz.h);
    } catch { /* defaults */ }
    this.placeInfoPanel();
    new ResizeObserver(() => this.clampInfoPanel()).observe(this.infoPanel);
    window.addEventListener('resize', () => {
      this.placeInfoPanel();
      this.renderStatic(this.store.state);
    });
    window.addEventListener(LABEL_SCALE_EVENT, () => this.renderStatic(this.store.state));
    let layoutRaf = 0;
    window.addEventListener('tv-panel-layout', () => {
      if (layoutRaf) return;
      layoutRaf = requestAnimationFrame(() => {
        layoutRaf = 0;
        this.renderStatic(this.store.state);
      });
    });

    store.on((ev, s) => {
      if (ev.kind === 'state') this.onState(s, ev.prev);
      else if (ev.kind === 'meter') this.renderMeter();
      else if (ev.kind === 'connected' || ev.kind === 'hello') { this.renderStatus(s); this.renderMeter(); }
    });
    this.renderStatus(store.state);
    this.renderMeter();
    this.renderManual();
    this.renderMap(store.state);
    this.renderStatic(store.state);
    this.renderLog();
    setInterval(() => this.expire(), 250);
  }

  /** The screen's power-on animation, then hand it to the OledUi port. */
  async boot() {
    await this.oledMini.boot('v0.2 ready');
    this.oledUi = new OledUi(this.oledMini, (key, label, value) => this.callout(key, label, value));
    const tick = () => this.oledUi!.service(this.store.state, performance.now());
    tick();
    setInterval(tick, 20);
  }

  /** Point the manual at a control (null = the introduction). */
  setFocus(f: Focus | null) {
    this.focus = f;
    this.renderManual();
  }

  // ---------------------------------------------------------- state

  private onState(s: DeviceState, prev: DeviceState) {
    // glow on what moved; held pads stay lit while held
    for (let i = 0; i < 12; i++) {
      const on = ((s.pads >> i) & 1) !== 0;
      if (on !== (((prev.pads >> i) & 1) !== 0)) this.highlight(this.panel.pads[i], on ? Infinity : 400);
    }
    for (let i = 0; i < 8; i++)
      if (s.controls[i] !== prev.controls[i] || (((s.pickup >> i) & 1) && s.pots[i] !== prev.pots[i])) {
        const el = this.panel.controlEl(i);
        if (el) this.highlight(el);
      }
    if (s.motion !== prev.motion || s.sw1 !== prev.sw1) this.highlight(this.panel.sw1);
    if (s.noise !== prev.noise || s.sw2 !== prev.sw2) this.highlight(this.panel.sw2);

    this.renderStatus(s);
    this.renderMap(s);
    if (this.focus) this.renderManual();
    if (s.padMode !== prev.padMode || s.motion !== prev.motion || s.noise !== prev.noise
        || s.attackStep !== prev.attackStep || s.decayStep !== prev.decayStep)
      this.renderStatic(s);
  }

  /** The screen drew a callout: the log and the wide screen get it too. */
  private callout(key: string, label: string, value: string) {
    const html = lineHtml(label, value);
    this.oledWide.push(key, html);
    if (this.log[0]?.key === key) this.log[0].html = html;
    else this.log.unshift({ key, html });
    this.log = this.log.slice(0, 4);
    this.renderLog();
  }

  // ---------------------------------------------------------- rendering

  private renderStatus(s: DeviceState) {
    const st = statusText(s);
    const parts = [
      `<b>${s.padMode ? 'DIST' : 'OSC'}</b>`,
      `A ${ATTACK_NAMES[s.attackStep % 3]}`,
      `D ${DECAY_NAMES[s.decayStep % 3]}`,
      esc(st.value),
    ];
    if (s.steerActive) parts.push(`<i>steer${s.steerLatched ? '' : ' (held)'}</i>`);
    if (s.pickup) parts.push(`<i>MIDI holds ${CONTROLS.filter((_, i) => (s.pickup >> i) & 1).map((c) => c.name).join(' ')}</i>`);
    const link = this.store.connected
      ? `<span class="dim">device${this.store.firmware ? ` fw ${this.store.firmware}` : ''}</span>`
      : '<span class="dim">simulated</span>';
    parts.push(link);
    this.status.innerHTML = parts.join(' · ');
    this.oledWide.setStatus(parts.join(' · '));
  }

  private renderMeter() {
    const m = this.store.meter;
    const live = this.store.connected || this.store.demo;
    const bar = (label: string, frac: number, text: string, cls = '') =>
      `<div class="meter-row${cls}"><span class="k">${label}</span>` +
      `<span class="bar"><span style="width:${(Math.max(0, Math.min(1, frac)) * 100).toFixed(1)}%"></span></span>` +
      `<span class="v">${text}</span></div>`;
    const loopDb = Math.round((m.loopDb / 127) * 60 - 60);
    const steerFresh = performance.now() - this.steerFlashAt < 300;
    if (m.steerCount !== this.lastSteerCount) {
      if (this.lastSteerCount >= 0) this.steerFlashAt = performance.now();
      this.lastSteerCount = m.steerCount;
    }
    this.meterEl.innerHTML =
      bar('Excite', m.excite / 127, `${Math.round((m.excite / 127) * 100)}%`) +
      (live
        ? bar('Loop', m.loopDb / 127, m.loopDb === 0 ? '< -60 dB' : `${loopDb} dB`) +
          `<div class="meter-row"><span class="k">Osc</span><span class="v wide">${hz(m.oscHz)}` +
          `<span class="steer-dot${steerFresh ? ' on' : ''}" title="flashes when steering re-picks the pitch"></span></span></div>`
        : '<div class="meter-row dim"><span class="k">Loop</span><span class="v wide">needs the device: the DSP runs on the Daisy</span></div>');
  }

  private renderManual() {
    const f = this.focus;
    const s = this.store.state;
    if (!f) {
      this.manualEl.innerHTML =
        `<div class="manual-title">Manual</div><p>${INTRO}</p><p class="dim">${HOWTO}</p>`;
      return;
    }
    let title = '';
    let value = '';
    let doc = '';
    let midi = '';
    if (f.kind === 'control') {
      const c = CONTROLS[f.i];
      title = `${c.name} · ${c.fn}`;
      value = knobValue(f.i, s.controls[f.i] / 127);
      doc = c.doc;
      midi = `CC ${CC_KNOB_BASE + f.i}`;
      if ((s.pickup >> f.i) & 1) value += ' (set by MIDI: turn the pot through it to take over)';
    } else if (f.kind === 'pad') {
      const p = PADS[f.i];
      const t = padText(f.i, s);
      title = `${p.name} · ${t.label.slice(t.label.indexOf(' ') + 1)}`;
      value = t.value;
      doc = p.doc;
      midi = `ch 10 note ${36 + f.i}`;
    } else {
      const sw = SWITCHES[f.i];
      title = `${sw.name} · ${sw.fn}`;
      value = f.i === 0 ? motionName(s) : noiseName(s);
      doc = sw.doc;
      midi = `CC ${f.i === 0 ? 28 : 29}`;
    }
    this.manualEl.innerHTML =
      `<div class="manual-title">${esc(title)}<span class="manual-back" title="Back to the introduction">×</span></div>` +
      `<div class="manual-value">${esc(value)}</div><p>${esc(doc)}</p><p class="dim">MIDI: ${midi}</p>`;
  }

  /** The knob map: what every control does right now, with its value. */
  private renderMap(s: DeviceState) {
    this.mapHead.innerHTML = `${this.mapOpen ? '▾' : '▸'} <b>Controls</b>`;
    this.mapBody.style.display = this.mapOpen ? '' : 'none';
    if (!this.mapOpen) return;
    const rows: string[] = [];
    CONTROLS.forEach((c, i) => {
      const armed = ((s.pickup >> i) & 1) !== 0;
      rows.push(`<div class="model-row${armed ? ' rec' : ''}" data-ctl="${i}">` +
        `<span class="k"><b>${c.name}</b> ${c.fn}${armed ? ' <i>MIDI</i>' : ''}</span>` +
        `<span class="v">${esc(knobValue(i, s.controls[i] / 127))}</span></div>`);
    });
    rows.push(`<div class="model-row" data-sw="0"><span class="k"><b>SW1</b> Osc motion</span><span class="v">${motionName(s)}</span></div>`);
    rows.push(`<div class="model-row" data-sw="1"><span class="k"><b>SW2</b> Noise</span><span class="v">${noiseName(s)}</span></div>`);
    rows.push(`<div class="model-row" data-pad="1"><span class="k"><b>P1</b> Steer</span><span class="v">${s.steerActive ? 'on' : 'off'}</span></div>`);
    rows.push(`<div class="model-row" data-pad="${s.padMode ? 2 : 0}"><span class="k"><b>P3-P9</b> ${s.padMode ? 'Distortion' : 'Osc pitch'}</span>` +
      `<span class="v">${esc(s.padMode ? distValue(s) : pitchValue(s))}</span></div>`);
    rows.push(`<div class="model-row" data-pad="10"><span class="k"><b>P10</b> Attack</span><span class="v">${ATTACK_NAMES[s.attackStep % 3]}</span></div>`);
    rows.push(`<div class="model-row" data-pad="11"><span class="k"><b>P11</b> Decay</span><span class="v">${DECAY_NAMES[s.decayStep % 3]}</span></div>`);
    this.mapBody.innerHTML = rows.join('');
  }

  private renderLog() {
    this.logEl.innerHTML = this.log.map((e) => `<div class="action-line">${e.html}</div>`).join('');
  }

  /** Static overlays: 'ids' = designators, 'full' = what each control does now. */
  private renderStatic(s: DeviceState) {
    this.staticWrap.innerHTML = '';
    if (this.overlayMode === 'dynamic') return;
    const ids = this.overlayMode === 'ids';
    const scale = this.svgToOverlay(KNOB_R, 0).x - this.svgToOverlay(0, 0).x;
    const fontPx = Math.max(8.5, Math.min(36, scale * 0.72 * labelScale()));
    const mk = (x: number, y: number, text: string, cls = '') => {
      if (!text) return;
      const el = document.createElement('div');
      el.className = `static-label${cls}`;
      el.style.left = `${x.toFixed(1)}px`;
      el.style.top = `${y.toFixed(1)}px`;
      el.style.fontSize = `${fontPx.toFixed(1)}px`;
      el.textContent = text;
      this.staticWrap.appendChild(el);
    };
    for (const [i, knob] of this.panel.knobs) {
      const c = this.svgToOverlay(knob.cx, knob.cy);
      mk(c.x, c.y, CONTROLS[i].name, ' in-knob');
      if (!ids) {
        const p = this.svgToOverlay(knob.cx, knob.cy + KNOB_R + 1.5);
        mk(p.x, p.y, CONTROLS[i].fn);
      }
    }
    for (const [i, fader] of this.panel.faders) {
      const bb = fader.rect.getBBox();
      const p = this.svgToOverlay(bb.x + bb.width / 2, fader.maxY + 4);
      mk(p.x, p.y, ids ? CONTROLS[i].name : `${CONTROLS[i].name}\n${CONTROLS[i].fn}`);
    }
    const ov = this.overlay.getBoundingClientRect();
    this.panel.pads.forEach((el, i) => {
      const t = el.getBoundingClientRect();
      const x = t.left - ov.left + t.width / 2;
      const y = t.top - ov.top + t.height / 2;
      let text = PADS[i].name;
      if (!ids) text = i >= 3 && i <= 9 ? playPadShort(i, s) : `${text}\n${PADS[i].short}`;
      mk(x, y, text, ' pad');
    });
    ([[this.panel.sw1, 0], [this.panel.sw2, 1]] as const).forEach(([g, k]) => {
      const t = g.getBoundingClientRect();
      const x = t.left - ov.left + t.width / 2;
      const y = t.top - ov.top + t.height + 2;
      const sw = SWITCHES[k];
      mk(x, y, ids ? sw.name : `${sw.name}\n${k === 0 ? motionName(s) : noiseName(s)}`);
    });
  }

  // ---------------------------------------------------------- highlights

  private highlight(el: Element, ttl = HL_TTL_MS) {
    el.classList.add('live-hl');
    this.hls.set(el, ttl === Infinity ? Infinity : performance.now() + ttl);
  }

  private expire() {
    const now = performance.now();
    for (const [el, until] of this.hls) {
      if (now > until) {
        el.classList.remove('live-hl');
        this.hls.delete(el);
      }
    }
    // the steer dot is a short flash; let it go out without a new frame
    if ((this.store.connected || this.store.demo) && now - this.steerFlashAt < 600) this.renderMeter();
  }

  /** Map row hover -> light the matching control on the drawing. */
  private setHl(row: HTMLElement | null) {
    let target: Element | null = null;
    if (row?.dataset.ctl !== undefined) target = this.panel.controlEl(+row.dataset.ctl);
    else if (row?.dataset.pad !== undefined) target = this.panel.pads[+row.dataset.pad] ?? null;
    else if (row?.dataset.sw !== undefined) target = row.dataset.sw === '0' ? this.panel.sw1 : this.panel.sw2;
    if (target === this.hlTarget) return;
    this.hlTarget?.classList.remove('ctl-hl');
    this.hlTarget = target;
    target?.classList.add('ctl-hl');
  }

  private svgToOverlay(x: number, y: number) {
    return mapSvgToOverlay(this.panel.svg, this.overlay, x, y);
  }

  private deviceRect() {
    const vb = this.panel.svg.viewBox.baseVal;
    const a = this.svgToOverlay(vb.x, vb.y);
    const b = this.svgToOverlay(vb.x + vb.width, vb.y + vb.height);
    return { left: a.x, top: a.y, right: b.x, bottom: b.y, width: b.x - a.x, height: b.y - a.y };
  }

  // ---------------------------------------------------------- draggable info panel
  // (carried over from TouchPlaited's labels.ts)

  private placeInfoPanel() {
    const o = this.overlay.getBoundingClientRect();
    let fx: number;
    let fy: number;
    const stored = localStorage.getItem(INFO_POS_KEY);
    let parsed = false;
    if (stored) {
      try {
        ({ fx, fy } = JSON.parse(stored));
        parsed = true;
      } catch { /* default below */ }
    }
    this.infoPanel.style.maxHeight = '';
    if (!parsed) {
      // Default: the free space next to or below the drawing on a phone; on a
      // desktop, to the right of the drawing if there is room, else over its
      // top-left free zone.
      const r = this.deviceRect();
      const mobile = matchMedia('(max-width: 820px), (max-height: 500px)').matches;
      if (mobile && o.height > o.width) {
        // Portrait: the band under the drawing, scrolling inside it rather
        // than growing up over the pads.
        const top = Math.min(r.bottom + 10, o.height - 120);
        fx = Math.max(8, r.left) / Math.max(1, o.width);
        fy = top / Math.max(1, o.height);
        this.infoPanel.style.maxHeight = `${Math.max(112, o.height - top - 8)}px`;
      } else if (!mobile && o.width - r.right > 300) {
        fx = (r.right + 16) / Math.max(1, o.width);
        fy = (r.top + 8) / Math.max(1, o.height);
      } else if (mobile) {
        fx = 8 / Math.max(1, o.width);
        fy = Math.max(8, r.top) / Math.max(1, o.height);
      } else {
        fx = (r.left + r.width * 0.08) / Math.max(1, o.width);
        fy = (r.top + r.height * 0.08) / Math.max(1, o.height);
      }
    }
    this.infoPanel.style.left = `${Math.min(Math.max(0, fx!), 0.95) * o.width}px`;
    this.infoPanel.style.top = `${Math.min(Math.max(0, fy!), 0.95) * o.height}px`;
    this.clampInfoPanel();
  }

  private clampInfoPanel() {
    const o = this.overlay.getBoundingClientRect();
    const p = this.infoPanel;
    p.style.left = `${Math.min(p.offsetLeft, Math.max(0, o.width - p.offsetWidth - 8))}px`;
    p.style.top = `${Math.min(p.offsetTop, Math.max(0, o.height - p.offsetHeight - 8))}px`;
  }

  private initInfoDrag(handle: HTMLDivElement) {
    const p = this.infoPanel;
    let startX = 0, startY = 0, origX = 0, origY = 0;
    handle.addEventListener('pointerdown', (e) => {
      handle.setPointerCapture(e.pointerId);
      p.classList.add('dragging');
      startX = e.clientX;
      startY = e.clientY;
      origX = p.offsetLeft;
      origY = p.offsetTop;
    });
    handle.addEventListener('pointermove', (e) => {
      if (!p.classList.contains('dragging')) return;
      p.style.left = `${origX + e.clientX - startX}px`;
      p.style.top = `${origY + e.clientY - startY}px`;
    });
    handle.addEventListener('pointerup', (e) => {
      p.classList.remove('dragging');
      handle.releasePointerCapture(e.pointerId);
      const o = this.overlay.getBoundingClientRect();
      localStorage.setItem(INFO_POS_KEY, JSON.stringify({
        fx: p.offsetLeft / Math.max(1, o.width),
        fy: p.offsetTop / Math.max(1, o.height),
      }));
    });
  }

  private applyInfoScale(scale: number) {
    this.infoScale = Math.min(2.2, Math.max(0.6, Number.isFinite(scale) ? scale : 1));
    this.infoPanel.style.fontSize = `${(14 * this.infoScale).toFixed(1)}px`;
  }

  private applyInfoSize(w: number | null, h: number | null) {
    this.infoPanel.style.width = w ? `${Math.round(w)}px` : '';
    this.infoPanel.style.height = h ? `${Math.round(h)}px` : '';
  }

  private initInfoResize(grip: HTMLDivElement) {
    let startX = 0, startY = 0, startW = 0, startH = 0;
    let w: number | null = null, h: number | null = null;
    grip.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      grip.setPointerCapture(e.pointerId);
      startX = e.clientX;
      startY = e.clientY;
      startW = this.infoPanel.offsetWidth;
      startH = this.infoPanel.offsetHeight;
    });
    grip.addEventListener('pointermove', (e) => {
      if (!grip.hasPointerCapture(e.pointerId)) return;
      w = Math.min(900, Math.max(180, startW + e.clientX - startX));
      h = Math.min(1000, Math.max(90, startH + e.clientY - startY));
      this.applyInfoSize(w, h);
    });
    grip.addEventListener('pointerup', (e) => {
      grip.releasePointerCapture(e.pointerId);
      if (w !== null) localStorage.setItem(INFO_SIZE_KEY, JSON.stringify({ w, h }));
    });
  }
}
