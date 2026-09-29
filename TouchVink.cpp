// TouchVink — a Jaap Vink style feedback instrument for the Synthux Simple Touch.
// See docs/ for the signal flow, control map and roadmap.

#include "daisy_seed.h"
#include "daisysp.h"
#include "config.h"
#include "engine.h"
#include "simple_touch.h"
#include "midi_io.h"
#include "telemetry.h"
#include "oled_screen.h"
#include "oled_ui.h"
#include "oled_boot.h"
#include <cmath>
#include <cstdlib>

using namespace daisy;
using namespace touchvink;

// The Synthux libDaisy fork takes the USB device names from the application.
extern "C" {
const char* USBD_MANUFACTURER_STRING = "TouchVink";
const char* USBD_PRODUCT_STRING_HS = "TouchVink";
const char* USBD_PRODUCT_STRING_FS = "TouchVink";
}

static DaisySeed hw;
static Engine engine;
static Knobs knobs;
static Switches switches;
static Pads pads;
static MidiIO midi;
static Telemetry telemetry;
static OledScreen oled;
static OledUi oled_ui;

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

static constexpr uint32_t kCalibHoldMs = 1000;   // P10 + P11
static constexpr uint32_t kHoldAnnounceMs = 150; // a P11 tap never shows a bar

enum class PadMode { Osc, Dist };

struct UiState {
    PadMode mode = PadMode::Osc;
    int attack_step = 0;
    int decay_step = 1;
    bool steer_latched = false;
    int active_pad = -1;       // last touched play pad still held
    float latch_pitch = 0.f;   // drone: peak pressure of the last OSC-mode touch
    float latch_drive = 0.f;   // drone: peak pressure of the last DIST-mode touch
    uint32_t calib_start = 0;  // when P10+P11 went down together
    bool calib_active = false;
    bool calib_done = false;   // fired, still held
    bool combo_used = false;   // swallow the releases after a P10+P11 combo
    int osc_pad = 4;           // pitch from P3..P9 (index), or -1 = the ch 1 note
    int midi_note = 45;
    bool key_down = false;     // a ch 1 note is held
    float key_pressure = 0.f;  // its aftertouch
};
static UiState ui;

// ------------------------------------------------------------ LED feedback
// Every write goes through set_led, so telemetry can mirror the exact blink.
static bool led_lit = false;
static void set_led(bool on) {
    led_lit = on;
    hw.SetLed(on);
}

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

// ------------------------------------------------------------ MIDI control state
// A CC write takes its knob over until the pot is turned through the CC value
// (the same pickup rule TouchPlaited uses between modes and for its CCs).
struct Pickup {
    bool armed = false;
    bool pot_below = false;  // which side of `value` the pot was on when armed
    float value = 0.f;
};
static Pickup pickup[8];
static int cc_sent[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // last pot CC out, 7-bit

// A CC can also set what a switch does, until the lever is moved.
static int motion_cc = -1, noise_cc = -1;  // zone 0..2, -1 = follow the lever
static int sw1_last = -1, sw2_last = -1;

// Last zone sent or received per discrete CC, so a CC in is never echoed out.
static int cc_zone[128];

static float knob_value(int i) { return pickup[i].armed ? pickup[i].value : knobs.Get(i); }
static uint8_t to7(float v) { return static_cast<uint8_t>(fminf(fmaxf(v, 0.f), 1.f) * 127.f + 0.5f); }
static int zone(uint8_t v, int n) { return v * n / 128; }
static uint8_t zone_value(int z, int n) { return static_cast<uint8_t>((z * 127 + (n - 1) / 2) / (n - 1)); }

// ------------------------------------------------------------ audio
static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    for (size_t i = 0; i < size; i++) {
        engine.Process(in[0][i], in[1][i], out[0][i], out[1][i]);
    }
}

// ------------------------------------------------------------ controls
static void ProcessKnobsAndSwitches() {
    knobs.Process();

    // A pot behind a pickup takes over once it reaches the CC value, or passes it.
    for (int i = 0; i < 8; i++) {
        Pickup& pu = pickup[i];
        if (!pu.armed) continue;
        const float pot = knobs.Get(i);
        if (fabsf(pot - pu.value) < cfg::kPickupNear || (pot < pu.value) != pu.pot_below) pu.armed = false;
    }

    auto& p = engine.params();
    p.osc_noise = knob_value(0);   // S30  osc <-> noise   (IN2)
    p.ext_gain = knob_value(1);    // S31  ext input level  (IN1)
    p.ring = knob_value(2);        // S32  ring modulation
    p.reverb = knob_value(3);      // S33  reverb
    p.loop_gain = knob_value(4);   // S34  VCA / feedback into the compressor
    p.delay = knob_value(5);       // S35  loop delay time
    p.source_mix = 1.f - knob_value(6);  // S36  fader: internal (bottom) <-> ext (top)
    p.out_vol = knob_value(7);     // S37  fader: output amp

    // Moving a lever hands its role back from MIDI.
    const int sw1 = switches.SW1() - 1, sw2 = switches.SW2() - 1;
    if (sw1 != sw1_last) { motion_cc = -1; sw1_last = sw1; }
    if (sw2 != sw2_last) { noise_cc = -1; sw2_last = sw2; }

    static const OscMotion motions[3] = {OscMotion::Lfo, OscMotion::Steady, OscMotion::Drunk};
    static const NoiseMode noises[3] = {NoiseMode::Pink, NoiseMode::Drift, NoiseMode::Brown};
    p.motion = motions[motion_cc >= 0 ? motion_cc : sw1];
    p.noise = noises[noise_cc >= 0 ? noise_cc : sw2];
}

static void ProcessPads() {
    pads.Process();
    auto& p = engine.params();
    const uint32_t now = System::GetNow();

    // --- recalibrate: hold P10 + P11 for 1 s
    if (pads.Touched(kPadAttack) && pads.Touched(kPadDecay)) {
        ui.combo_used = true;
        if (!ui.calib_active) { ui.calib_active = true; ui.calib_start = now; }
        if (!ui.calib_done && now - ui.calib_start >= kCalibHoldMs) {
            ui.calib_done = true;
            pads.Recalibrate();
            led.Blink(5);
        }
        return;
    }
    ui.calib_active = ui.calib_done = false;
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
    // P11: tap steps short -> long -> drone, long press always back to short (ends the drone)
    if (env_pads_free && pads.JustReleased(kPadDecay)) {
        const bool long_press = pads.HeldMs(kPadDecay) >= cfg::kLongPressMs;
        ui.decay_step = long_press ? 0 : (ui.decay_step + 1) % 3;
        engine.SetDecayStep(ui.decay_step);
        led.Blink(ui.decay_step + 1);
    }
    const bool drone = ui.decay_step == 2;
    if (!drone) ui.latch_pitch = ui.latch_drive = 0.f;

    // --- play pads P3..P9
    for (int pad = kFirstPlayPad; pad <= kLastPlayPad; pad++) {
        if (!pads.JustTouched(pad)) continue;
        ui.active_pad = pad;
        const int idx = pad - kFirstPlayPad;
        (ui.mode == PadMode::Osc ? ui.latch_pitch : ui.latch_drive) = 0.f;  // new touch, new peak
        if (ui.mode == PadMode::Osc) {
            ui.osc_pad = idx;
            p.pad_freq_hz = cfg::kPadFreqs[idx];
            engine.TriggerExcite(pads.Velocity(pad));
        } else {
            p.dist = static_cast<DistType>(idx);  // P3 = clean ... P9 = crush
        }
    }
    if (ui.active_pad >= 0 && !pads.Touched(ui.active_pad)) ui.active_pad = -1;

    // A held ch 1 note's aftertouch counts as pad pressure when no pad is held.
    const float pr = ui.active_pad >= 0 ? pads.Pressure(ui.active_pad) : (ui.key_down ? ui.key_pressure : 0.f);
    if (drone) {
        // hold each mode's pressure at the highest point of its last touch, also after release
        float& latch = ui.mode == PadMode::Osc ? ui.latch_pitch : ui.latch_drive;
        if (pr > latch) latch = pr;
        p.pitch_pressure = ui.latch_pitch;
        p.dist_pressure = ui.latch_drive;
    } else if (ui.mode == PadMode::Osc) {
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
    set_led(on);
}

// ------------------------------------------------------------ MIDI in
static void OnNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    const float v = vel / 127.f;
    if (ch == cfg::kMidiPadsCh) {
        pads.SetVirtual(note - cfg::kMidiPadNoteBase, true, v);
    } else if (ch == cfg::kMidiKeysCh) {
        // The note is the pitch; it plays like an OSC-mode pad in either pad mode.
        ui.osc_pad = -1;
        ui.midi_note = note;
        ui.key_down = true;
        ui.key_pressure = 0.f;
        engine.params().pad_freq_hz = 440.f * exp2f((note - 69) / 12.f);
        engine.TriggerExcite(v);
    }
}

static void OnNoteOff(uint8_t ch, uint8_t note) {
    if (ch == cfg::kMidiPadsCh) pads.SetVirtual(note - cfg::kMidiPadNoteBase, false);
    else if (ch == cfg::kMidiKeysCh && note == ui.midi_note) ui.key_down = false;
}

static void OnPolyPressure(uint8_t ch, uint8_t note, uint8_t pressure) {
    if (ch == cfg::kMidiPadsCh) pads.SetVirtualPressure(note - cfg::kMidiPadNoteBase, pressure / 127.f);
    else if (ch == cfg::kMidiKeysCh && note == ui.midi_note) ui.key_pressure = pressure / 127.f;
}

static void OnChannelPressure(uint8_t ch, uint8_t pressure) {
    if (ch == cfg::kMidiPadsCh) {
        for (int i = 0; i < Pads::kNumPads; i++) pads.SetVirtualPressure(i, pressure / 127.f);
    } else if (ch == cfg::kMidiKeysCh) {
        ui.key_pressure = pressure / 127.f;
    }
}

static void OnControlChange(uint8_t /*ch*/, uint8_t cc, uint8_t val) {
    const float v = val / 127.f;
    if (cc >= cfg::kCcKnobBase && cc < cfg::kCcKnobBase + 8) {
        const int i = cc - cfg::kCcKnobBase;
        pickup[i].armed = true;
        pickup[i].value = v;
        pickup[i].pot_below = knobs.Get(i) < v;
        return;
    }
    auto& p = engine.params();
    switch (cc) {
        case cfg::kCcMotion: motion_cc = cc_zone[cc] = zone(val, 3); break;
        case cfg::kCcNoise: noise_cc = cc_zone[cc] = zone(val, 3); break;
        case cfg::kCcPadMode:
            ui.mode = (cc_zone[cc] = zone(val, 2)) ? PadMode::Dist : PadMode::Osc;
            break;
        case cfg::kCcAttack:
            ui.attack_step = cc_zone[cc] = zone(val, 3);
            engine.SetAttackStep(ui.attack_step);
            break;
        case cfg::kCcDecay:
            ui.decay_step = cc_zone[cc] = zone(val, 3);
            engine.SetDecayStep(ui.decay_step);
            break;
        case cfg::kCcSteer: ui.steer_latched = (cc_zone[cc] = zone(val, 2)) != 0; break;
        case cfg::kCcDist: p.dist = static_cast<DistType>(cc_zone[cc] = zone(val, 7)); break;
        default: break;
    }
}

static void OnAllNotesOff() {
    pads.ClearVirtual();
    ui.key_down = false;
}

static void OnSysex(const uint8_t* data, size_t len) { telemetry.HandleSysex(data, len); }

static const MidiHandlers kMidiHandlers = {OnNoteOn, OnNoteOff, OnPolyPressure, OnChannelPressure,
                                           OnControlChange, OnAllNotesOff, OnSysex};

// ------------------------------------------------------------ MIDI out
// Pots and every discrete setting go out as CC on ch 1, finger touches as ch 10
// notes with poly aftertouch. Only what the panel did is sent: a value MIDI
// just set is never echoed back.
static void SendDiscrete(uint8_t cc, int z, int n, bool send) {
    if (cc_zone[cc] == z) return;
    cc_zone[cc] = z;
    if (send) midi.SendCC(cfg::kMidiKeysCh, cc, zone_value(z, n));
}

static void ServiceMidiOut() {
    for (int i = 0; i < 8; i++) {
        if (pickup[i].armed) continue;  // the pot is not driving anything yet
        const int v = to7(knobs.Get(i));
        if (cc_sent[i] < 0 || abs(v - cc_sent[i]) >= cfg::kCcOutDeadband || ((v == 0 || v == 127) && v != cc_sent[i])) {
            if (cc_sent[i] >= 0) midi.SendCC(cfg::kMidiKeysCh, cfg::kCcKnobBase + i, v);
            cc_sent[i] = v;
        }
    }
    // The first pass only records where everything is: boot is not a gesture.
    static bool primed = false;
    const auto& p = engine.params();
    SendDiscrete(cfg::kCcMotion, static_cast<int>(p.motion), 3, primed);
    SendDiscrete(cfg::kCcNoise, static_cast<int>(p.noise), 3, primed);
    SendDiscrete(cfg::kCcPadMode, ui.mode == PadMode::Dist ? 1 : 0, 2, primed);
    SendDiscrete(cfg::kCcAttack, ui.attack_step, 3, primed);
    SendDiscrete(cfg::kCcDecay, ui.decay_step, 3, primed);
    SendDiscrete(cfg::kCcSteer, ui.steer_latched ? 1 : 0, 2, primed);
    SendDiscrete(cfg::kCcDist, static_cast<int>(p.dist), 7, primed);
    primed = true;

    static uint16_t notes_out = 0;
    static uint8_t pressure_out[Pads::kNumPads] = {};
    static uint32_t pressure_ms[Pads::kNumPads] = {};
    const uint32_t now = System::GetNow();
    for (int i = 0; i < Pads::kNumPads; i++) {
        const uint16_t m = 1u << i;
        const uint8_t note = cfg::kMidiPadNoteBase + i;
        // (a tap shorter than one read is touched and released in the same
        // Process, so test "not MIDI" rather than "finger still there")
        if (pads.JustTouched(i) && !pads.VirtTouched(i)) {
            const int vel = to7(pads.Velocity(i));
            midi.SendNoteOn(cfg::kMidiPadsCh, note, vel < 1 ? 1 : vel);
            notes_out |= m;
            pressure_out[i] = 0;
        } else if ((notes_out & m) && !pads.Touched(i)) {
            midi.SendNoteOff(cfg::kMidiPadsCh, note);
            notes_out &= ~m;
        } else if ((notes_out & m) && now - pressure_ms[i] >= cfg::kPressureOutMs) {
            const uint8_t pr = to7(pads.Pressure(i));
            if (pr != pressure_out[i]) {
                midi.SendPolyPressure(cfg::kMidiPadsCh, note, pr);
                pressure_out[i] = pr;
                pressure_ms[i] = now;
            }
        }
    }
}

// ------------------------------------------------------------ telemetry + OLED
static void BuildTelemetry(TelemetryState& t, uint32_t now) {
    const auto& p = engine.params();
    t.pads = pads.State();
    t.pickup = 0;
    for (int i = 0; i < 8; i++) {
        t.controls[i] = to7(knob_value(i));
        t.pots[i] = to7(knobs.Get(i));
        if (pickup[i].armed) t.pickup |= 1u << i;
    }
    t.sw1 = static_cast<uint8_t>(switches.SW1() - 1);
    t.sw2 = static_cast<uint8_t>(switches.SW2() - 1);
    t.motion = static_cast<uint8_t>(p.motion);
    t.noise = static_cast<uint8_t>(p.noise);
    t.led = led_lit ? 127 : 0;
    t.pad_mode = ui.mode == PadMode::Dist ? 1 : 0;
    t.steer_latched = ui.steer_latched;
    t.steer_active = p.steer;
    t.attack_step = static_cast<uint8_t>(ui.attack_step);
    t.decay_step = static_cast<uint8_t>(ui.decay_step);
    t.osc_pad = ui.osc_pad < 0 ? 0x7F : static_cast<uint8_t>(ui.osc_pad);
    t.midi_note = static_cast<uint8_t>(ui.midi_note);
    t.dist = static_cast<uint8_t>(p.dist);
    t.pitch_pressure = to7(p.pitch_pressure);
    t.dist_pressure = to7(p.dist_pressure);

    // Holds toward a threshold, for the bar on the screens.
    t.hold_kind = t.hold_progress = t.hold_stage = 0;
    if (ui.calib_active) {
        t.hold_kind = 1;
        t.hold_progress = to7(static_cast<float>(now - ui.calib_start) / kCalibHoldMs);
        t.hold_stage = ui.calib_done ? 1 : 0;
    } else if (!ui.combo_used && pads.Touched(kPadDecay) && pads.HeldMs(kPadDecay) >= kHoldAnnounceMs) {
        const float f = static_cast<float>(pads.HeldMs(kPadDecay) - kHoldAnnounceMs)
                      / static_cast<float>(cfg::kLongPressMs - kHoldAnnounceMs);
        t.hold_kind = 2;
        t.hold_progress = to7(f);
        t.hold_stage = f >= 1.f ? 1 : 0;
    }

    // Meter.
    t.steer_count = static_cast<uint8_t>(engine.SteerCount() & 0x7F);
    t.excite = to7(engine.ExciteLevel());
    const float env = engine.LoopLevel();
    const float db = env > 1e-6f ? 20.f * log10f(env) : -120.f;
    t.loop_db = to7((db + 60.f) / 60.f);
    t.osc_centihz = static_cast<uint32_t>(fminf(engine.OscHz(), 20000.f) * 100.f + 0.5f);
}

static void ServiceMidi() {
    midi.Service(kMidiHandlers);
    ServiceMidiOut();
    const uint32_t now = System::GetNow();
    TelemetryState t;
    BuildTelemetry(t, now);
    telemetry.Service(t, now, midi);
    oled_ui.Service(t, now, oled);
}

int main() {
    hw.Init();
    hw.SetAudioBlockSize(cfg::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    engine.Init(hw.AudioSampleRate(), delay_l, delay_r, cfg::kMaxDelaySamples, &reverb);

    for (int& z : cc_zone) z = -1;
    knobs.Init(hw);
    switches.Init();
    const bool pads_ok = pads.Init();
    oled.Init();  // after the pads: both share I2C1, and this pins it to 400 kHz
    if (!pads_ok) {
        // No MPR121: say so, and blink fast forever so it is obvious without a screen too.
        oled.ShowLine("TouchVink", "no touch IC");
        while (true) { set_led(true); System::Delay(60); set_led(false); System::Delay(60); }
    }
    OledBoot::Run(oled, System::GetNow(), "v0.2 ready", set_led);

    midi.Init();
    ProcessKnobsAndSwitches();
    hw.StartAudio(AudioCallback);

    while (true) {
        ProcessKnobsAndSwitches();
        ProcessPads();
        ProcessLed();
        ServiceMidi();
        System::Delay(1);  // ~1 kHz control rate
    }
}
