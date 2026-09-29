# TouchVink

A feedback instrument for the **Synthux Simple Touch**, after the self-regulating tape/ring-mod/reverb patches of **Jaap Vink** (Institute of Sonology). One knob, one function.

A delayed copy of the output is ring-modulated, reverberated and passed through a VCA that is turned *down* by the loop's own loudness. That compressor lets the loop run above unity gain without blowing up, so it keeps moving by itself. Pads excite the loop, set the oscillator pitch, pick a distortion, or let the loop "steer" its own oscillator.

> Status: v0.2 — compiles for the Daisy Seed, DSP tested on desktop, **not yet tested on hardware**. See `docs/PLAN.md`.

**Interactive manual:** [webmanual/](webmanual/) — the panel drawn live in the browser, with the screen and a manual entry for every control. It plays on its own, or mirrors and plays the device over USB MIDI.

## Signal flow

```mermaid
flowchart LR
    EXT["EXT IN (stereo)<br/>S31 level"] --> MIX
    OSC["OSC<br/>SW1 lfo/steady/drunk"] --> BAL["S30 osc ↔ noise"]
    NOISE["NOISE<br/>SW2 pink/drift/brown"] --> BAL
    BAL --> AD["AD env<br/>P10 att / P11 dec"] --> MIX["S36 source mix"]
    BAL -. carrier .-> RING
    EXT -. carrier .-> RING
    MIX -- excitation --> RING
    DEL["REC loop delay<br/>S35"] --> RING["AC-MUP ring / add<br/>S32"]
    RING --> REV["REV<br/>S33"] --> VCA["V-AMM VCA"]
    VCA --> DIST["DIST<br/>pads, pressure"] --> OUT["OUT<br/>S37"]
    DIST --> ENV["AMD env follower"] -- inverted --> VCA
    DIST -- "× S34 loop gain" --> DEL
    ENV -- "swell (P1 steer)" --> SAH["T-SAH"] --> OSC
```

## Controls

| | |
|---|---|
| **S30** | Internal source: osc ↔ noise |
| **S31** | External input level (0 → +12 dB) |
| **S32** | Ring modulation: loop straight ↔ loop × carrier |
| **S33** | Reverb |
| **S34** | VCA / loop gain — above ~60 % the loop sustains itself |
| **S35** | Loop delay, 2 ms (pitched comb) → 1.9 s (tape echo) |
| **S36** fader | Source: internal (bottom) ↔ external (top) |
| **S37** fader | Output level |
| **SW1** | Osc motion: LFO sweep / steady / drunk walk (pitch and sine↔saw shape) |
| **SW2** | Noise: pink / pink↔brown drift / brown |
| **P0** | Pads → OSC mode (LED 1 blink) |
| **P2** | Pads → DIST mode (LED 2 blinks) |
| **P1** | Steering (loop swells re-pick the osc pitch): tap = on/off, hold = momentary. Not a note latch. |
| **P3–P9** | OSC mode: pick pitch (0.8, 5, 27.5, 55, 110, 220, 440 Hz) + excite; pressure bends up to +1 oct |
| | DIST mode: clean, soft, hard, fold, half-rect, full-rect (octave), crush; pressure = drive |
| **P10** | Attack step: 2 ms / 60 ms / 600 ms |
| **P11** | Decay step, tap: 120 ms → 2 s → drone. Hold 0.5 s: back to 120 ms. Drone holds the excitation and freezes pad pressure at its peak |
| **P10 + P11** 1 s | Recalibrate pads |

## Screen, MIDI, web manual

**OLED (optional).** A 128×32 SSD1306 on I2C at D11/D12 (the pads' bus, address 0x3C), as on TouchPlaited. It plays the TouchVink boot animation, names whatever you touch with its value in real units (dB, ms, Hz, loop gain), draws a bar for the P10+P11 and long-P11 holds, shows a pickup track when MIDI holds a knob, and otherwise shows the pad mode, A/D steps and the pitch or distortion in play. Without a screen fitted nothing changes.

**USB MIDI**, both ways:

| | In | Out |
|---|---|---|
| ch 1 notes | Osc pitch (the note is the pitch) + excite | |
| ch 10 notes 36–47 | Pads P0–P11, as if touched; poly aftertouch = pressure | Pad touches, with poly aftertouch |
| CC 20–27 | S30–S37; the pot takes over again once turned through the value | Pot moves |
| CC 28–34 | SW1, SW2, pad mode, attack, decay, steer, distortion | Changes made on the panel |

The device also streams its state as SysEx, which the [web manual](webmanual/) reads. Full map: [webmanual/README.md](webmanual/README.md#midi-map).

## Build

```bash
git clone --recurse-submodules <this repo>
make libs      # once
make           # build/TouchVink.bin
make program-dfu
```

`make NO_USB_MIDI=1` gives the USB port to serial logging instead. TRS MIDI (D13/D14, modded boards) is opt-in with `make TRS_MIDI=1`; see `midi/midi_io.h` for why.

Uses the **Synthux fork of libDaisy** and DaisySP (with DaisySP-LGPL for the reverb), as submodules.

### Listen without hardware

```bash
make -C host && ./host/render    # run from the repo root, writes host/out/*.wav
```

## Layout

```
TouchVink.cpp        main: audio callback, pad/knob/switch logic, LED
common/config.h      every tunable number
dsp/engine.*         the Vink loop (portable, no libDaisy)
dsp/blocks.h         small DSP blocks: noise colours, env follower, AD, delay, distortion
hw/simple_touch.*    knobs, switches, MPR121 pads with pressure + velocity (+ MIDI touches)
midi/                USB (and optional TRS) MIDI I/O, SysEx telemetry for the web manual
display/             OLED: screen driver, UI, boot animation
webmanual/           interactive web manual (Vite + TypeScript), deployed to GitHub Pages
host/                desktop renderer for listening tests
docs/SKETCHES.md     notebook sketches translated, with every interpretation marked
docs/PLAN.md         bring-up checklist, tuning, filter options, roadmap
```

## To-Do / Roadmap

In no particular order
- [x] midi controls
- [x] oled screen info
- [x] interactive webmanual

## Credits

- Jaap Vink — the original patch and technique
- [AudreyTouch](https://github.com/Synthux-Academy/AudreyTouch) (Nick Donaldson / Infrasonic, Synthux Academy) — Simple Touch feedback-synth structure
- [TouchString](https://github.com/vincentltm/TouchString) (vincentltm) — MPR121 pressure-sensing approach
- [TouchPlaited](https://github.com/jonwaterschoot/TouchPlaited) — MIDI I/O, SysEx telemetry, OLED driver and boot screen, and the visualizer the web manual is built from
- [libDaisy (Synthux fork)](https://github.com/Synthux-Academy/libDaisy), [DaisySP](https://github.com/electro-smith/DaisySP)
