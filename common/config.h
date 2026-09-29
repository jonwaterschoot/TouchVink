#pragma once
// TouchVink — all tunable constants in one place.
// Change numbers here first before touching DSP code.

#include <array>
#include <cstdint>

namespace touchvink {
namespace cfg {

// ---------------------------------------------------------------- audio
static constexpr float kSampleRate = 48000.f;
static constexpr size_t kBlockSize = 16;

// ---------------------------------------------------------------- loop ("REC")
// Max loop delay. 2 s stereo float = 768 kB -> lives in SDRAM on the Seed.
static constexpr float  kMaxDelaySec = 2.0f;
static constexpr size_t kMaxDelaySamples = static_cast<size_t>(kMaxDelaySec * kSampleRate) + 16;
static constexpr float kMinDelayMs = 2.f;     // S35 fully CCW (comb / pitched)
static constexpr float kMaxDelayMs = 1900.f;  // S35 fully CW (tape echo)
static constexpr float kDelayStereoSpread = 1.013f; // R reads slightly later -> width
static constexpr float kDelayGlideSec = 0.25f;      // tape-like pitch glide when turning S35

// Tape-hiss floor injected in the loop. This is what lets the patch
// self-excite from "nothing", like Vink's tape machine did.
static constexpr float kHissDb = -84.f;

// ---------------------------------------------------------------- VCA / compressor
// Loop gain (S34) max. Above ~1.0 the loop grows until the compressor catches it.
static constexpr float kMaxLoopGain = 1.8f;
// Compressor strength: gain = 1 / (1 + kAgcDepth * envelope).
// Steady-state level with loop gain G is roughly (G - 1) / kAgcDepth.
static constexpr float kAgcDepth = 3.0f;
static constexpr float kEnvAttackMs = 3.f;
static constexpr float kEnvReleaseMs = 120.f;

// ---------------------------------------------------------------- ring mod ("AC-MUP")
static constexpr float kRingMakeup = 3.0f;  // product of two ~0.3 signals is quiet

// ---------------------------------------------------------------- loop tone
// Fixed "tape bandwidth" filters inside the loop. Not on a knob (see docs/PLAN.md, filter question).
static constexpr float kLoopHpHz = 28.f;
static constexpr float kLoopLpHz = 11000.f;

// ---------------------------------------------------------------- reverb
static constexpr float kRevFbMin = 0.55f;
static constexpr float kRevFbMax = 0.94f;
static constexpr float kRevLpHz = 9000.f;

// ---------------------------------------------------------------- sources
// Pads P3..P9 in OSC mode: base frequency per pad (Hz).
// Low pads are sub-audio: in ring-mod they become tremolo / chopping,
// like Vink's slow FUG. Higher pads give classic ring-mod tones.
static constexpr std::array<float, 7> kPadFreqs = {0.8f, 5.f, 27.5f, 55.f, 110.f, 220.f, 440.f};
static constexpr float kPressureBendOct = 1.0f;  // full pressure bends the osc up 1 octave
static constexpr float kOscGlideSec = 0.02f;
static constexpr float kOscSawMix = 0.3f;        // fixed wave: sine + this much saw (0 = pure sine)

// SW1 osc motion: LFO / steady / drunk. Both moving modes bend pitch and wave shape.
static constexpr float kMotionLfoHz = 0.17f;
static constexpr float kMotionLfoOct = 0.25f;    // LFO pitch depth, +- octaves
static constexpr float kMotionDrunkStepSec = 0.12f; // a new random step this often
static constexpr float kMotionDrunkStepOct = 0.12f; // max size of one step
static constexpr float kMotionDrunkRangeOct = 1.0f; // walk stays within +- this
static constexpr float kMotionDrunkGlideSec = 0.06f;
static constexpr float kNoiseLfoHz = 0.11f;      // SW2 middle: pink <-> brown drift

// ---------------------------------------------------------------- excitation envelope (P10 / P11)
static constexpr std::array<float, 3> kAttackSteps = {0.002f, 0.06f, 0.6f};  // seconds
// -1 = drone: excitation holds, and play-pad pressure latches at its peak.
static constexpr std::array<float, 3> kDecaySteps = {0.12f, 2.0f, -1.f};
static constexpr uint32_t kLongPressMs = 500;     // P11 held this long = back to step 1

// ---------------------------------------------------------------- steering (Vink T-SAH)
// Fires when the loop envelope swells above its own slow average by kSteerRatio.
static constexpr float kSteerRatio = 1.35f;
static constexpr float kSteerRearm = 1.05f;
static constexpr float kSteerSlowMs = 900.f;
static constexpr float kSteerFloorDb = -60.f;
static constexpr float kSteerMinIntervalSec = 0.08f;
static constexpr float kSteerDepthOct = 1.0f;     // new pitch = pad pitch * 2^(+-depth)

// ---------------------------------------------------------------- distortion
// If true the distortion sits inside the loop (after the VCA), so the
// compressor tames it and every pass adds harmonics. False = output only.
static constexpr bool kDistInLoop = true;
static constexpr float kDistBaseDrive = 1.5f;
static constexpr float kDistPressureDrive = 6.f;  // extra drive at full pad pressure

// ---------------------------------------------------------------- output
static constexpr float kOutMaxGain = 2.0f;

// ---------------------------------------------------------------- MIDI (webmanual/README.md has the full map)
// ch 1: keyboard. A note sets the osc pitch (the note IS the pitch) and excites
//       the loop, like an OSC-mode pad. CCs are accepted on any channel.
// ch 10: the pads. Note 36 + i is pad Pi, both ways: the device sends it when a
//       pad is touched, and a received one acts exactly like touching that pad
//       (so a DAW recording plays back the same gestures). Poly aftertouch is
//       pad pressure, both ways.
static constexpr uint8_t kMidiKeysCh = 0;
static constexpr uint8_t kMidiPadsCh = 9;
static constexpr uint8_t kMidiPadNoteBase = 36;
// CC numbers. 20..27 follow S30..S37; a CC write takes that knob over until
// the pot is turned through the CC value (pickup).
static constexpr uint8_t kCcKnobBase = 20;  // 20..27 = S30..S37
static constexpr uint8_t kCcMotion = 28;    // SW1, 3 zones: LFO / steady / drunk
static constexpr uint8_t kCcNoise = 29;     // SW2, 3 zones: pink / drift / brown
static constexpr uint8_t kCcPadMode = 30;   // < 64 OSC, >= 64 DIST
static constexpr uint8_t kCcAttack = 31;    // 3 zones = attack steps
static constexpr uint8_t kCcDecay = 32;     // 3 zones = decay steps (top = drone)
static constexpr uint8_t kCcSteer = 33;     // >= 64 steering latched on
static constexpr uint8_t kCcDist = 34;      // 7 zones = distortion type
// A pot counts as moved (and is sent as CC) once it is this far from the last
// value sent, in 7-bit steps. 1 would stream pot jitter.
static constexpr int kCcOutDeadband = 2;
// A pot within this distance of a CC value it is behind picks it up
// (0..1 units), so a pot resting on the value does not need to cross it.
static constexpr float kPickupNear = 0.015f;
// Poly aftertouch out: at most this often per held pad.
static constexpr uint32_t kPressureOutMs = 20;

}  // namespace cfg
}  // namespace touchvink
