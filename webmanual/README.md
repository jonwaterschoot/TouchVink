# TouchVink web manual

An interactive manual: the Simple Touch panel drawn live, with the OLED screen, the LED and a manual entry for whatever you point at. Built on the [TouchPlaited visualizer](https://github.com/jonwaterschoot/TouchPlaited/tree/main/visualizer).

It works in two ways:

- **On its own.** Press the pads, drag the knobs and faders, click the switches. The page runs the firmware's control logic itself (`src/core/sim.ts`), so the screen, the LED and the info panel answer the way the instrument would. The DSP stays on the Daisy, so the loop meter needs the device.
- **With the device** (menu → *Connect MIDI*, Chrome/Edge, allow SysEx). The drawing mirrors the panel from the firmware's telemetry, and everything you do on the page is sent to the device as MIDI: pads as ch 10 notes, knobs and switches as CCs, the piano in the MIDI drawer as ch 1 pitch.

```sh
npm install
npm run dev        # http://localhost:5173
npm run build      # dist/, deployed to GitHub Pages by .github/workflows/pages.yml
```

## What's where

| File | |
|---|---|
| `src/core/protocol.ts` | Telemetry SysEx decoder. Must match `midi/telemetry.cpp`. |
| `src/core/describe.ts` | Every text the screen shows. Mirror of the formatters in `display/oled_ui.cpp`. |
| `src/panel/oled-ui.ts` | When the screen shows what. Port of `OledUi` in `display/oled_ui.cpp`. |
| `src/panel/oled-mini.ts` | The 128×32 screen, dot for dot. Port of `display/oled_screen.cpp` and the boot animation. |
| `src/core/sim.ts` | The panel logic from `TouchVink.cpp`, for use without a device. |
| `src/core/controls-meta.ts` | Names, manual text, MIDI map, and the numbers mirrored from `common/config.h`. |

The files that mirror firmware say so at the top; change both sides together.

## MIDI map

| | In | Out |
|---|---|---|
| **ch 1 notes** | Set the osc pitch (the note is the pitch) and excite the loop. Aftertouch bends like pad pressure. | |
| **ch 10 notes 36–47** | Pad P0–P11, exactly as if touched. Poly aftertouch is pressure. | Sent when a pad is touched, with poly aftertouch. |
| **CC 20–27** | S30–S37. The knob follows MIDI until its pot is turned through the value (pickup). | Sent when a pot moves. |
| **CC 28 / 29** | SW1 osc motion / SW2 noise, 3 zones. Until the lever moves. | Sent when it changes. |
| **CC 30–34** | Pad mode (2 zones), attack, decay (3), steer (on ≥ 64), distortion (7). | Sent when it changes. |

CCs are accepted on any channel and sent on ch 1. A value that came in over MIDI is never echoed back out.

## URL flags

All combinable.

| Flag | Effect |
|------|--------|
| `?demo` | start the scripted demo |
| `?midi` | connect Web MIDI (needs a prior permission grant) |
| `?drawer` | open the MIDI drawer (CC faders + piano) |
| `?menu` | open the ☰ menu |
| `?transparent` | transparent page background, for OBS browser-source overlays |
| `?bare` | hide the ☰ menu |
| `?view=pads` / `?view=panel` | crop to the pad field / the knob panel |
| `?zoom=1.5` | scale everything |

**OBS setup.** Add a Browser Source pointing at `http://localhost:5173/?midi&transparent&bare` (or the hosted page with the same flags). The panel floats over your footage as an overlay, with no visible browser window. OBS browser sources are Chromium, so Web MIDI works inside them. The telemetry sends the full state in every frame, so restarting OBS or the source mid-stream picks the state straight back up. The info panel can be dragged by its ⠿ title bar, so you can put it wherever the shot needs it. Label mode, text sizes and positions are stored per browser, so set them inside OBS itself. Leave out `&bare` for a moment, right-click the source → *Interact*, and use ☰ → *Settings*.

## Panel drawing

`assets/panel.svg` is generated from `../img/simpletouchdrawing_blank.svg` by `npm run panel` (`tools/build-panel-svg.mjs`, from TouchPlaited). Don't edit it by hand.
