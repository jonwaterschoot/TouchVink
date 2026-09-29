# Notes for Claude Code (continuing on the PC)

TouchVink: Jaap Vink-style feedback instrument, firmware for the Synthux Simple Touch
(Daisy Seed + MPR121). Owner: Jon. Read README.md, docs/SKETCHES.md, docs/PLAN.md first.

## Ground rules
- One knob = one function. Don't add shift layers on knobs without asking.
- All tunable numbers go in common/config.h, not inline.
- dsp/ must stay free of libDaisy (only DaisySP + std) so host/ keeps building on desktop.
- Nothing allocates or blocks in the audio callback. Big buffers go in SDRAM (DSY_SDRAM_BSS).
- libDaisy is the Synthux fork (submodule). Keep USE_DAISYSP_LGPL = 1 (ReverbSc).

## Build / test
- Firmware: `make libs` once, then `make` → build/TouchVink.bin. `make program-dfu` to flash.
- Desktop: `make -C host && ./host/render` from the repo root → host/out/*.wav plus
  per-second dBFS and NaN report. Run this after any dsp/ change; all scenarios must
  report no NaN and stay below 0 dBFS.

## Next tasks (see docs/PLAN.md for detail)
1. Hardware bring-up checklist in docs/PLAN.md (knob/switch polarity, pad max_delta calibration, CPU load).
2. Filter: try the envelope-driven loop low-pass (option 2 in PLAN.md) behind a config flag.
3. Flash is nearly full (~127.6 of 128 kB with USB MIDI + OLED). Next growth: BOOT_SRAM (see Makefile).

## MIDI / screen / web manual
- midi/telemetry.cpp <-> webmanual/src/core/protocol.ts: same byte layout, change together.
- display/oled_ui.cpp <-> webmanual/src/core/describe.ts + src/panel/oled-ui.ts: same texts and logic.
- TouchVink.cpp pad/CC logic <-> webmanual/src/core/sim.ts (the page plays without a device).
- Web manual: `cd webmanual && npm install && npm run dev`; `npm run build` must pass (tsc strict).
