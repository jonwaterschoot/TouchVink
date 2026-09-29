# Sketch translation

Four notebook pages were the starting point. This file writes out what they say, and marks where the firmware made an interpretation (**→ decision**) so it can be revisited.

---

## 1. Jaap Vink patch (GRM Recollection sleeve / video still)

Vink worked at the Institute of Sonology (Utrecht) with a studio of discrete modules, patched into one big feedback system. Module names as they appear on the diagram, with the most likely meaning:

| Label | Likely meaning | Role in the patch | TouchVink equivalent |
|---|---|---|---|
| FUG | Function generator (osc / LFO) | Free-running source | Noise LFO, osc |
| T-SAH | Triggered sample & hold | Samples FUG when triggered | Steering S&H (P1) |
| +5V | DC offset | Sets the base pitch | Pad pitch (P3–P9) |
| MXA | Mixer / amplifier | Sums S&H + FUG + offset → pitch CV | Pitch = pad × steer ratio × pressure bend |
| V-FUG | Voltage-controlled function generator | Ring-mod carrier | Oscillator (SW1 shape) |
| REC | Tape recorder (record → playback head) | The loop delay | Loop delay (S35) |
| AC-MUP | AC multiplier | Ring modulator: REC × V-FUG | Ring amount (S32) |
| REV | Reverb (spring / plate) | Smear inside the loop | Reverb (S33) |
| AMD | Amplitude detector | Envelope follower on the loop | Env follower (fixed attack/release) |
| MXA *inv.* | Inverting mixer | Turns loudness into *less* gain | `gain = 1 / (1 + k·env)` |
| V-AMM | Voltage-controlled amplitude modulator | VCA: the automatic gain control | VCA / loop gain (S34) |
| DCA → DCT | Probably DC amplifier → threshold/trigger | Envelope level fires the T-SAH | Onset detector → steering |
| pot on the output | Manual feedback amount | Loop gain | S34 |

*DCA/DCT are a best guess from context; the rest are standard Sonology names.*

The core idea: a delayed copy of the output is ring-modulated, reverberated and sent through a VCA whose gain is pulled **down** by the loop's own loudness. That self-compressing loop can run above unity gain without exploding, so it keeps evolving by itself. A second path lets the loop's loudness re-trigger the oscillator pitch — the system "steers" itself.

---

## 2. Notebook page "Audrey" (two diagrams)

**Top — how AudreyTouch works:**
`NOISE → feedback comb filter (resonator) → DISTORTION → REV → FILTER → ECHO DELAY → OUT`, with an *override FB* path from after the filter back into the resonator.

**Bottom — Vink redrawn by hand:**
Same as the table above, with your annotations:
- "STEERING THE OSC" around FUG / T-SAH / MXA / DCT / DCA — the self-steering pitch path.
- ENV FOLLOW → INVERT → VCA labelled **"sidechain" / "compressor"**.
- REC labelled DELAY, AC-MUP labelled RING MODULATOR, V-AMM labelled VCA / Amp.

---

## 3. Notebook page — TouchVink signal flow (landscape)

```
SW2 NOISE (pink / pink+brown mix with LFO / brown) ─┐
SW1 SHAPE (toggle 1/2/3) → OSC ─────────────────────┤ S30 mix
                                                    ▼
EXT AUDIO (vol S31) ───────────────────────────► MIX (S36) ──► RING MOD (S32) ◄── DELAY (S35) ◄──┐
                                                                  │                               │
                                                                  ▼                               │
                                                             REV (S33) ──► AMP (S34) ──► OUT (S37)─┤
                                                                            ▲                     │
                                                          COMPRESSOR: ENV FOL → INV ◄─────────────┘
```

**→ decisions made while turning this into code**

1. **The ring mod needs something to multiply.** A ring mod of a silent loop stays silent. So the source is used twice:
   - the free-running osc/noise (or the ext input) is the **carrier** of the ring mod — like Vink's V-FUG, it never stops;
   - the same source, gated by the pad envelope, is **added** into the loop as **excitation**.
   S32 crossfades the loop between *straight through* (0) and *multiplied by the carrier* (1). Excitation is added at every setting, so pads always do something.
2. **S34 "AMP/VCA" = loop gain.** The compressor is always on; S34 sets how much of the VCA output goes back into the delay. Below ~60 % the loop decays (echo / effect). Above it, the loop grows until the compressor holds it — the Vink zone.
3. **Self-excitation from "tape hiss".** A −84 dBFS noise floor is injected in the loop. With S34 high the loop blooms out of nothing in a few seconds, like Vink's tape machine did.
4. Distortion sits **inside the loop, after the VCA** ("inserted at end"), so each pass adds harmonics and the compressor keeps it in check. One flag in `config.h` (`kDistInLoop`) moves it to the output only.

---

## 4. Notebook page "TouchVink" — panel layout

| Control | Sketch label | Firmware |
|---|---|---|
| S30 | OSC / NOISE, "IN 2" | Balance osc ↔ noise (internal source) |
| S31 | EXT, "IN 1" | Ext stereo input level, 0 → +12 dB |
| S32 | RING, MODUL | Ring modulation amount |
| S33 | REV | Reverb (mix and decay together) |
| S34 | VCA | Loop gain into the compressor |
| S35 | DELAY | Loop delay 2 ms → 1.9 s (log), tape-style glide |
| S36 (left fader) | MIX SOURCE | Ext ↔ internal source |
| S37 (right fader) | AMP, VOL OUT | Output level |
| SW1 | OSC source shape | Sine / Saw / Square |
| SW2 | NOISE | Pink / Pink↔Brown drift (LFO) / Brown |
| P3–P9 | "Different osc FR." / "Different DISTORTION (inserted at end)" | Depends on pad mode (below) |
| P0 | PADS OSC | Pads → osc mode |
| P2 | PADS DIST | Pads → distortion mode |
| P10–P11 | "A/D cycle 3 settings?" | P10 cycles attack, P11 cycles decay |

**→ additions not in the sketch**
- **P1 = steering** (the Vink T-SAH path you wanted on the pads). Tap to latch on/off, hold for momentary.
- **Pressure** (TouchString method): in OSC mode, pressing harder bends the osc up to +1 octave; in DIST mode, pressure adds drive.
- **P11 step 3 = drone**: the excitation holds instead of decaying, so the internal source can run continuously without keeping a finger on a pad.
- **P10 + P11 held 1 s** = recalibrate the touch pads.
