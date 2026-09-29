# Plan & roadmap

## Where it stands (v0.1)

| Part | State |
|---|---|
| Signal flow + control map | Done — see `SKETCHES.md` |
| DSP engine (`dsp/`) | Written, runs on desktop, no NaN / runaway in 5 stress scenarios |
| Hardware layer (`hw/`) | Written: knobs, switches, MPR121 with pressure + velocity |
| Firmware (`TouchVink.cpp`) | **Compiles** for the Daisy Seed (94 kB / 128 kB flash) — **not yet run on hardware** |
| Host listening test (`host/`) | Renders WAVs of 5 scenarios |

## Step 1 — first flash (on your PC)

```bash
git clone --recurse-submodules <your repo> TouchVink && cd TouchVink
make libs          # once: builds the Synthux libDaisy fork + DaisySP
make               # -> build/TouchVink.bin
make program-dfu   # Seed in DFU mode (hold BOOT, tap RESET)
```
or drop `build/TouchVink.bin` on <https://flash.daisy.audio>.

## Step 2 — hardware checklist

Tick these in order; each one isolates a layer.

1. **LED fast-blinks forever** → MPR121 not answering (I2C pins / address). Otherwise continue.
2. **Knob direction.** Every knob should increase clockwise. If one is reversed, flip it in `Knobs::Process()`.
3. **Switch positions.** Check SW1 up = sine, SW2 up = pink. If a switch reads upside down, swap the pins in `Switches::Init()`.
4. **Pads.** P0 → 1 blink, P2 → 2 blinks, P1 tap → 3 blinks (steer on) / 1 blink (off).
5. **Pressure calibration.** `Pads::max_delta` holds per-pad full-scale values copied from TouchString's unit. Temporarily log `pads.Pressure(i)` (set `DEBUG = 1`, `hw.StartLog()`), press each pad hard, and put the peak delta values into the array.
6. **CPU load.** Add `daisy::CpuLoadMeter` around `engine.Process` in the callback and log max load. ReverbSc is the heaviest block; if load is above ~70 % raise `kBlockSize` to 32 or 48.
7. **Input level.** Feed a line signal, S36 fully left, S34 at zero: you should hear the input with reverb/ring only. Adjust the S31 taper if +12 dB is too hot.
8. **The Vink zone.** S34 at ~75 %, S32 at 50 %, S35 mid, no input: the loop should bloom from silence within ~5–10 s and stabilise.

## Step 3 — tuning (by ear)

All numbers live in `common/config.h`. The ones that shape the character most:

- `kAgcDepth` — higher = tighter compressor, quieter/steadier loop; lower = wilder, louder.
- `kMaxLoopGain` — how far past unity S34 goes.
- `kEnvReleaseMs` — the "breathing" speed of the compressor. Vink's patches pump; try 60–400 ms.
- `kPadFreqs` — the 7 pad pitches. The two lowest are sub-audio on purpose (ring → tremolo/chop).
- `kSteerRatio` / `kSteerSlowMs` — how easily steering fires.
- `kHissDb` — how quickly the loop self-starts.

Use the host renderer to try changes before flashing: `make -C host && ./host/render` (from the repo root), listen to `host/out/*.wav`.

## Open question — the filter

You want a filter, but there's no free knob. Options, from least to most "rule-breaking":

1. **Fixed tape-bandwidth filter in the loop** — already in (HP 28 Hz, LP 11 kHz, `config.h`). It keeps the loop from collecting rumble or fizz but isn't playable.
2. **Envelope-driven filter (no knob).** Put a resonant low-pass in the loop whose cutoff follows the AMD envelope — louder loop = darker (or brighter). This is very Vink: the loop controls its own tone. Could share S33 as a bipolar knob: left of centre = filter depth, right = reverb. *Recommended first try.*
3. **Filter as a third pad mode.** e.g. hold P0 + P2 → "FILTER mode", pads P3–P9 pick cutoff steps, pressure sweeps. Keeps 1 knob = 1 function, costs a pad gesture.
4. **Shift layer on a knob** (hold P1 + turn S35 = cutoff). Works, but breaks the rule you set.

## Roadmap

- [ ] Hardware bring-up (checklist above)
- [ ] Filter (option 2 or 3)
- [ ] Stereo steering: independent S&H per channel for wider movement
- [ ] USB MIDI: CC out for all knobs + pad pressure (reuse the TouchPlaited `midi/` module and visualizer)
- [ ] Optional: log the loop envelope and steering events over MIDI to drive the TouchPlaited-style web visualizer
- [ ] Faceplate artwork from the Simple Touch template (in the AudreyTouch repo, `Faceplate/`)
- [ ] Manual (`docs/MANUAL.md`) once the controls settle
