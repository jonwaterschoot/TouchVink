// TouchVink — a Jaap Vink style feedback instrument for the Synthux Simple Touch.
// See docs/ for the signal flow, control map and roadmap.

#include "daisy_seed.h"
#include "daisysp.h"
#include "config.h"
#include "engine.h"
#include "simple_touch.h"

using namespace daisy;
using namespace touchvink;

static DaisySeed hw;
static Engine engine;
static Knobs knobs;
static Switches switches;
static Pads pads;

// Big buffers live in the 64 MB SDRAM.
static float DSY_SDRAM_BSS delay_l[cfg::kMaxDelaySamples];
static float DSY_SDRAM_BSS delay_r[cfg::kMaxDelaySamples];
static daisysp::ReverbSc DSY_SDRAM_BSS reverb;

// ------------------------------------------------------------ pad roles
static constexpr int kPadOscMode = 0;
static constexpr int kPadSteer = 1;
static constexpr int kPadDistMode = 2;
static constexpr int kFirstPlayPad = 3;
static constexpr int kLastPlayPad = 9;
static constexpr int kPadAttack = 10;
static constexpr int kPadDecay = 11;

enum class PadMode { Osc, Dist };

struct UiState {
    PadMode mode = PadMode::Osc;
    int attack_step = 0;
    int decay_step = 1;
    bool steer_latched = false;
    int active_pad = -1;       // last touched play pad still held
    uint32_t calib_timer = 0;  // P10+P11 held
    bool combo_used = false;   // swallow the releases after a P10+P11 combo
};
static UiState ui;

// ------------------------------------------------------------ LED feedback
struct Led {
    int blinks = 0;
    uint32_t t = 0;
    void Blink(int n) { blinks = n * 2; t = 0; }
    // returns true while a blink pattern is playing
    bool Update(bool& out) {
        if (blinks <= 0) return false;
        if (++t >= 90) { t = 0; blinks--; }
        out = (blinks & 1);
        return true;
    }
} led;

// ------------------------------------------------------------ audio
static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    for (size_t i = 0; i < size; i++) {
        engine.Process(in[0][i], in[1][i], out[0][i], out[1][i]);
    }
}

// ------------------------------------------------------------ controls
static void ProcessKnobsAndSwitches() {
    knobs.Process();
    auto& p = engine.params();
    p.osc_noise = knobs.Get(0);   // S30  osc <-> noise   (IN2)
    p.ext_gain = knobs.Get(1);    // S31  ext input level  (IN1)
    p.ring = knobs.Get(2);        // S32  ring modulation
    p.reverb = knobs.Get(3);      // S33  reverb
    p.loop_gain = knobs.Get(4);   // S34  VCA / feedback into the compressor
    p.delay = knobs.Get(5);       // S35  loop delay time
    p.source_mix = knobs.Get(6);  // S36  fader: ext <-> internal source
    p.out_vol = knobs.Get(7);     // S37  fader: output amp

    static const OscShape shapes[3] = {OscShape::Sine, OscShape::Saw, OscShape::Square};
    static const NoiseMode noises[3] = {NoiseMode::Pink, NoiseMode::Drift, NoiseMode::Brown};
    p.shape = shapes[switches.SW1() - 1];
    p.noise = noises[switches.SW2() - 1];
}

static void ProcessPads() {
    pads.Process();
    auto& p = engine.params();

    // --- recalibrate: hold P10 + P11 for 1 s
    if (pads.Touched(kPadAttack) && pads.Touched(kPadDecay)) {
        ui.combo_used = true;
        if (++ui.calib_timer == 1000) {
            pads.Recalibrate();
            led.Blink(5);
        }
        return;
    }
    ui.calib_timer = 0;
    if (ui.combo_used && !pads.Touched(kPadAttack) && !pads.Touched(kPadDecay)) {
        ui.combo_used = false;
        return;  // both released: ignore this frame's releases
    }
    const bool env_pads_free = !ui.combo_used;

    // --- mode pads
    if (pads.JustTouched(kPadOscMode)) { ui.mode = PadMode::Osc; led.Blink(1); }
    if (pads.JustTouched(kPadDistMode)) { ui.mode = PadMode::Dist; led.Blink(2); }

    // --- steering: tap toggles latch, hold = momentary
    if (pads.JustReleased(kPadSteer) && pads.HeldMs(kPadSteer) < 350) {
        ui.steer_latched = !ui.steer_latched;
        led.Blink(ui.steer_latched ? 3 : 1);
    }
    p.steer = ui.steer_latched || (pads.Touched(kPadSteer) && pads.HeldMs(kPadSteer) >= 350);

    // --- envelope steps (release so they don't fire during the P10+P11 combo)
    if (env_pads_free && pads.JustReleased(kPadAttack)) {
        ui.attack_step = (ui.attack_step + 1) % 3;
        engine.SetAttackStep(ui.attack_step);
        led.Blink(ui.attack_step + 1);
    }
    if (env_pads_free && pads.JustReleased(kPadDecay)) {
        ui.decay_step = (ui.decay_step + 1) % 3;
        engine.SetDecayStep(ui.decay_step);
        led.Blink(ui.decay_step + 1);
    }

    // --- play pads P3..P9
    for (int pad = kFirstPlayPad; pad <= kLastPlayPad; pad++) {
        if (!pads.JustTouched(pad)) continue;
        ui.active_pad = pad;
        const int idx = pad - kFirstPlayPad;
        if (ui.mode == PadMode::Osc) {
            p.pad_freq_hz = cfg::kPadFreqs[idx];
            engine.TriggerExcite(pads.Velocity(pad));
        } else {
            p.dist = static_cast<DistType>(idx);  // P3 = clean ... P9 = crush
        }
    }
    if (ui.active_pad >= 0 && !pads.Touched(ui.active_pad)) ui.active_pad = -1;

    const float pr = ui.active_pad >= 0 ? pads.Pressure(ui.active_pad) : 0.f;
    if (ui.mode == PadMode::Osc) {
        p.pitch_pressure = pr;
        p.dist_pressure = 0.f;
    } else {
        p.dist_pressure = pr;
        p.pitch_pressure = 0.f;
    }
}

static void ProcessLed() {
    bool on = false;
    if (!led.Update(on)) {
        // default: LED shows excitation envelope (OSC mode) or loop level (DIST mode);
        // a steering event gives a short flash.
        static uint32_t flash = 0;
        if (engine.SteerFired()) flash = 30;
        if (flash > 0) { flash--; on = !on; }
        const float lvl = ui.mode == PadMode::Osc ? engine.ExciteLevel() : engine.LoopLevel() * 4.f;
        on = on != (lvl > 0.1f);
    }
    hw.SetLed(on);
}

int main() {
    hw.Init();
    hw.SetAudioBlockSize(cfg::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    engine.Init(hw.AudioSampleRate(), delay_l, delay_r, cfg::kMaxDelaySamples, &reverb);

    knobs.Init(hw);
    switches.Init();
    if (!pads.Init()) {
        // No MPR121: blink fast forever so the problem is obvious.
        while (true) { hw.SetLed(true); System::Delay(60); hw.SetLed(false); System::Delay(60); }
    }

    ProcessKnobsAndSwitches();
    hw.StartAudio(AudioCallback);

    while (true) {
        ProcessKnobsAndSwitches();
        ProcessPads();
        ProcessLed();
        System::Delay(1);  // ~1 kHz control rate
    }
}
