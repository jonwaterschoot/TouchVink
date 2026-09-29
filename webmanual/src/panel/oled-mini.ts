// The panel's 128×32 SSD1306, emulated dot for dot: a port of
// display/oled_screen.cpp (from TouchPlaited's oled-mini.ts). Same bitmap
// fonts (oled-font-data.ts, verbatim from libDaisy), same font stepping, same
// geometry. What decides WHAT is shown lives in oled-ui.ts, a port of
// display/oled_ui.cpp; this file only draws, like OledScreen.
//
// Text is laid into a 128×32 bit grid from the glyph data, then painted as
// inset squares so the panel's discrete dots show. It sits in the faceplate's
// free zone between the knob columns and follows the drawing's pan and zoom.

import type { Panel } from './panel';
import { svgToOverlay } from './overlay-utils';
import { FONT_6X8, FONT_7X10, FONT_11X18, glyphPixel, type BitmapFont } from './oled-font-data';

const W = 128;
const H = 32;
const ASPECT = W / H;
const LABEL_CHARS = Math.floor(W / FONT_6X8.width);  // 21
const LABEL_CHARS_STEER = 19;                        // clear of the steer diamond
const STEER_CX = 124, STEER_CY = 3;
const PICKUP_VALUE_Y = 12;
const COLOR = '#ffb238';

const MIN_CELL = 3;
const MAX_CELL = 14;
const GAP = 0.22;  // fraction of each cell left dark between dots

const truncate = (s: string, n: number) => (s.length <= n ? s : s.slice(0, n));

/** Largest font whose character count fits (oled_screen.cpp ShowLine). */
function pickValueFont(len: number): BitmapFont {
  if (len <= Math.floor(W / FONT_11X18.width)) return FONT_11X18;
  if (len <= Math.floor(W / FONT_7X10.width)) return FONT_7X10;
  return FONT_6X8;
}

export class OledMini {
  private el: HTMLDivElement;
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private bits = new Uint8Array(W * H);
  private cell = 4;
  private squeezed = false;

  constructor(private overlay: HTMLElement, private panel: Panel, private screen: {
    x: number; y: number; w: number; h: number;
  }) {
    this.el = document.createElement('div');
    this.el.className = 'oled-mini';
    this.canvas = document.createElement('canvas');
    this.canvas.width = W * this.cell;
    this.canvas.height = H * this.cell;
    this.el.appendChild(this.canvas);
    overlay.appendChild(this.el);
    this.ctx = this.canvas.getContext('2d')!;
    this.rasterize();
    this.place();
    window.addEventListener('resize', () => this.place());
    let raf = 0;
    window.addEventListener('tv-panel-layout', () => {
      if (raf) return;
      raf = requestAnimationFrame(() => {
        raf = 0;
        this.place();
      });
    });
  }

  /** OledScreen::ShowLine. */
  showLine(label: string, value: string, steer = false) {
    this.bits.fill(0);
    let budget = LABEL_CHARS;
    if (steer) {
      budget = LABEL_CHARS_STEER;
      for (let dy = -3; dy <= 3; dy++) {
        const w = 3 - Math.abs(dy);
        for (let x = STEER_CX - w; x <= STEER_CX + w; x++) this.setBit(x, STEER_CY + dy);
      }
    }
    this.blitText(FONT_6X8, truncate(label.toUpperCase(), budget), 1, 0);
    if (value) {
      const font = pickValueFont(value.length);
      this.blitText(font, truncate(value, Math.floor(W / font.width)), 1, H - font.height);
    }
    this.rasterize();
  }

  /** OledScreen::ShowProgress: label, a bar filled progress/127, a note row. */
  showProgress(label: string, progress: number, note: string) {
    this.bits.fill(0);
    this.blitText(FONT_6X8, truncate(label.toUpperCase(), LABEL_CHARS), 1, 0);
    const X1 = 1, Y1 = 12, X2 = 126, Y2 = 21;
    this.drawRect(X1, Y1, X2, Y2, false);
    const FX1 = X1 + 2, FY1 = Y1 + 2, FY2 = Y2 - 2;
    const maxW = X2 - 2 - FX1;
    const w = Math.floor((maxW * (progress & 0x7f)) / 127);
    if (w > 0) this.drawRect(FX1, FY1, FX1 + w - 1, FY2, true);
    if (note) this.blitText(FONT_6X8, truncate(note.toUpperCase(), LABEL_CHARS), 1, H - FONT_6X8.height);
    this.rasterize();
  }

  /** OledScreen::ShowPickup: the value in effect, and a track with a post at
   * the target and a block at the pot, both 0..127. */
  showPickup(label: string, value: string, pot: number, target: number) {
    this.bits.fill(0);
    this.blitText(FONT_6X8, truncate(label.toUpperCase(), LABEL_CHARS), 1, 0);
    if (value) this.blitText(FONT_7X10, truncate(value, Math.floor(W / FONT_7X10.width)), 1, PICKUP_VALUE_Y);
    const X0 = 1, X1 = 126, SPAN = X1 - X0;
    const at = (v: number) => X0 + Math.floor(((v & 0x7f) * SPAN) / 127);
    for (let x = X0; x <= X1; x++) this.setBit(x, 30);
    const tx = at(target);
    this.drawRect(tx, 24, tx + 1, 31, true);
    const px = at(pot);
    this.drawRect(px >= X0 + 2 ? px - 2 : X0, 27, px <= X1 - 2 ? px + 2 : X1, 29, true);
    this.rasterize();
  }

  /** The power-on animation, a port of display/oled_boot.cpp: "Touch" +
   * "Vink" (one font size up) type in, hold, scatter as particles, then the
   * status line. Resolves when the status line is up. */
  async boot(status: string): Promise<void> {
    const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
    const slots: { ch: string; font: BitmapFont; x0: number; y0: number }[] = [];
    const w1 = 5 * FONT_6X8.width, w2 = 4 * FONT_7X10.width;
    const x0 = Math.floor((W - (w1 + w2)) / 2);
    const y2 = Math.floor((H - FONT_7X10.height) / 2);
    const base = y2 + FONT_7X10.height;
    [...'Touch'].forEach((ch, i) => slots.push({ ch, font: FONT_6X8, x0: x0 + i * FONT_6X8.width, y0: base - FONT_6X8.height }));
    [...'Vink'].forEach((ch, i) => slots.push({ ch, font: FONT_7X10, x0: x0 + w1 + i * FONT_7X10.width, y0: y2 }));

    this.bits.fill(0);
    for (const s of slots) {
      this.blitText(s.font, s.ch, s.x0, s.y0);
      this.rasterize();
      await sleep(35);
    }
    await sleep(1000);

    // One particle per lit pixel, flung out from the word's centre.
    const cx = x0 + (w1 + w2) / 2, cy = (y2 + base) / 2;
    const rnd = (lo: number, hi: number) => lo + Math.random() * (hi - lo);
    const parts: { x: number; y: number; vx: number; vy: number; life: number }[] = [];
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        if (!this.bits[y * W + x]) continue;
        const dx = x - cx, dy = y - cy;
        const len = Math.max(0.5, Math.hypot(dx, dy));
        parts.push({
          x, y,
          vx: (dx / len) * rnd(0.2, 0.65) + rnd(-0.125, 0.125),
          vy: (dy / len) * rnd(0.2, 0.65) - 0.1 + rnd(-0.1, 0.1),
          life: Math.floor(rnd(28, 52)),
        });
      }
    }
    for (let frame = 0; frame < 52; frame++) {
      this.bits.fill(0);
      for (const p of parts) {
        if (p.life <= 0) continue;
        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.02;
        p.life--;
        if (p.x < 0 || p.x >= W || p.y < 0 || p.y >= H) { p.life = 0; continue; }
        this.setBit(Math.floor(p.x), Math.floor(p.y));
      }
      this.rasterize();
      await sleep(16);  // the panel runs ~10 ms + the I2C transfer per frame
    }
    this.showLine('TouchVink', status);
  }

  // ---------------------------------------------------------- drawing

  /** WriteChar/WriteString (libDaisy display.h), glyph by glyph. */
  private blitText(font: BitmapFont, text: string, x0: number, y0: number) {
    for (let i = 0; i < text.length; i++) {
      const cx = x0 + i * font.width;
      if (cx + font.width > W) break;
      for (let row = 0; row < font.height; row++) {
        const y = y0 + row;
        if (y < 0 || y >= H) continue;
        for (let col = 0; col < font.width; col++)
          if (glyphPixel(font, text[i], col, row)) this.bits[y * W + cx + col] = 1;
      }
    }
  }

  private setBit(x: number, y: number) {
    if (x >= 0 && x < W && y >= 0 && y < H) this.bits[y * W + x] = 1;
  }

  private drawRect(x1: number, y1: number, x2: number, y2: number, fill: boolean) {
    for (let y = y1; y <= y2; y++)
      for (let x = x1; x <= x2; x++)
        if (fill || x === x1 || x === x2 || y === y1 || y === y2) this.setBit(x, y);
  }

  private rasterize() {
    const ctx = this.ctx;
    const cell = this.cell;
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.fillStyle = COLOR;
    const dot = cell * (this.squeezed ? 1 : 1 - GAP);
    const inset = (cell - dot) / 2;
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++)
        if (this.bits[y * W + x]) ctx.fillRect(x * cell + inset, y * cell + inset, dot, dot);
  }

  /** Fit the screen zone (SVG units) at the true 128:32 aspect, and size the
   * canvas so each logical pixel is a whole number of device pixels — no
   * resampling, so the dot grid stays crisp. Below MIN_CELL the canvas is
   * CSS-scaled instead so the screen keeps tracking the drawing. */
  private place() {
    const a = svgToOverlay(this.panel.svg, this.overlay, this.screen.x, this.screen.y);
    const b = svgToOverlay(this.panel.svg, this.overlay, this.screen.x + this.screen.w, this.screen.y + this.screen.h);
    const boxW = b.x - a.x;
    const boxH = b.y - a.y;
    let w = boxW;
    let h = w / ASPECT;
    if (h > boxH) {
      h = boxH;
      w = h * ASPECT;
    }
    const dpr = window.devicePixelRatio || 1;
    const ideal = Math.round((w * dpr) / W);
    const cell = Math.min(MAX_CELL, Math.max(MIN_CELL, ideal));
    const squeezed = ideal < MIN_CELL;
    if (ideal >= MIN_CELL && ideal <= MAX_CELL) {
      w = (W * cell) / dpr;
      h = (H * cell) / dpr;
    }
    this.el.style.left = `${(a.x + (boxW - w) / 2).toFixed(1)}px`;
    this.el.style.top = `${(a.y + (boxH - h) / 2).toFixed(1)}px`;
    this.el.style.width = `${w.toFixed(1)}px`;
    this.el.style.height = `${h.toFixed(1)}px`;
    if (squeezed !== this.squeezed) {
      this.squeezed = squeezed;
      this.el.classList.toggle('squeezed', squeezed);
      this.rasterize();
    }
    if (cell !== this.cell || this.canvas.width !== W * cell) {
      this.cell = cell;
      this.canvas.width = W * cell;
      this.canvas.height = H * cell;
      this.rasterize();
    }
  }
}
