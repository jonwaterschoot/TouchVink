// Text for the panel OLED, from TelemetryState's wire fields (7-bit values).
// Mirror of webmanual/src/core/describe.ts: same labels, same value formats,
// same rounding. Plain ASCII only (Font_6x8 has nothing else).

#include "oled_ui.h"
#include "config.h"
#include <cmath>
#include <cstring>

using namespace touchvink;
using namespace daisy;

namespace {

using Str = FixedCapStr<24>;

// ------------------------------------------------------------ static text

// Index = S30..S37.
const char* const kKnobNames[8] = {
    "Osc/noise", "Ext in", "Ring mod", "Reverb", "Loop gain", "Loop delay", "Source", "Output",
};
const char* const kDistNames[7] = {"clean", "soft", "hard", "fold", "half-rect", "full-rect", "crush"};
const char* const kMotionNames[3] = {"LFO", "steady", "drunk"};
const char* const kNoiseNames[3] = {"pink", "drift", "brown"};
const char* const kAttackNames[3] = {"2 ms", "60 ms", "600 ms"};
const char* const kDecayNames[3] = {"120 ms", "2 s", "drone"};
const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// ------------------------------------------------------------ formatters

inline float v01(uint8_t v7) { return static_cast<float>(v7 & 0x7F) / 127.f; }

// Fixed decimals, rounded half away from zero, like JS toFixed on these ranges.
void append_fixed(Str& out, float v, int decimals) {
    if (v < 0.f) {
        out.Append('-');
        v = -v;
    }
    int scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;
    const int n = static_cast<int>(v * scale + 0.5f);
    out.AppendInt(n / scale);
    if (decimals > 0) {
        out.Append('.');
        int frac = n % scale;
        for (int s = scale / 10; s > 0; s /= 10) {
            out.Append(static_cast<char>('0' + frac / s));
            frac %= s;
        }
    }
}

void append_pct(Str& out, float v) {
    out.AppendInt(static_cast<int>(v * 100.f + 0.5f));
    out.Append('%');
}

void append_db(Str& out, float gain) {
    if (gain < 0.0001f) {
        out.Append("off");
        return;
    }
    const float db = 20.f * log10f(gain);
    if (db >= 0.f) out.Append('+');
    append_fixed(out, db, 1);
    out.Append(" dB");
}

void append_hz(Str& out, float hz) {
    append_fixed(out, hz, hz < 100.f ? 1 : 0);
    out.Append(" Hz");
}

void append_ms(Str& out, float ms) {
    if (ms < 10.f) {
        append_fixed(out, ms, 1);
        out.Append(" ms");
    } else if (ms < 1000.f) {
        append_fixed(out, ms, 0);
        out.Append(" ms");
    } else {
        append_fixed(out, ms * 0.001f, 2);
        out.Append(" s");
    }
}

// What a knob value means, in the units the engine uses it in
// (dsp/engine.cpp: UpdateSlowParams and Process).
void knob_value(Str& out, int i, float v) {
    out.Clear();
    switch (i) {
        case 0:
            if (v <= 0.01f) out.Append("osc only");
            else if (v >= 0.99f) out.Append("noise only");
            else { out.Append("noise "); append_pct(out, v); }
            break;
        case 1: append_db(out, v * v * 4.f); break;
        case 2:
            if (v <= 0.01f) out.Append("add only");
            else if (v >= 0.99f) out.Append("ring only");
            else { out.Append("ring "); append_pct(out, v); }
            break;
        case 3:
            if (v <= 0.01f) out.Append("dry");
            else append_pct(out, v);
            break;
        case 4: {
            const float g = cfg::kMaxLoopGain * powf(v, 1.2f);
            out.Append('x');
            append_fixed(out, g, 2);
            out.Append(g < 1.f ? " decays" : " grows");
            break;
        }
        case 5: {
            const float ms = cfg::kMinDelayMs * powf(cfg::kMaxDelayMs / cfg::kMinDelayMs, v);
            append_ms(out, ms);
            // Short loops are a comb: name the pitch it rings at.
            if (ms < 20.f) {
                out.Append(' ');
                append_hz(out, 1000.f / ms);
            }
            break;
        }
        case 6:  // fader: bottom = internal source
            if (v <= 0.01f) out.Append("internal");
            else if (v >= 0.99f) out.Append("external");
            else { out.Append("ext "); append_pct(out, v); }
            break;
        case 7: append_db(out, v * v * cfg::kOutMaxGain); break;
        default: break;
    }
}

void knob_label(Str& out, int i, bool via_midi) {
    out.Clear();
    if (via_midi) {
        out.Append("CC");
        out.AppendInt(cfg::kCcKnobBase + i);
    } else {
        out.Append("S");
        out.AppendInt(30 + i);
    }
    out.Append(' ');
    out.Append(kKnobNames[i]);
}

void note_name(Str& out, int n) {
    out.Append(kNoteNames[n % 12]);
    out.AppendInt(n / 12 - 1);
}

// The osc's base pitch: the pad's, or the ch 1 note's.
float base_hz(const TelemetryState& t) {
    if (t.osc_pad == 0x7F) return 440.f * exp2f((static_cast<int>(t.midi_note) - 69) / 12.f);
    return cfg::kPadFreqs[t.osc_pad < 7 ? t.osc_pad : 4];
}

// Pitch with the pressure bend on top (motion and steering move it further;
// the web manual shows that live from the METER frame).
void pitch_value(Str& out, const TelemetryState& t) {
    out.Clear();
    if (t.osc_pad == 0x7F) {
        note_name(out, t.midi_note);
        out.Append(' ');
    }
    append_hz(out, base_hz(t) * exp2f(v01(t.pitch_pressure) * cfg::kPressureBendOct));
}

void dist_value(Str& out, const TelemetryState& t) {
    out.Clear();
    out.Append(kDistNames[t.dist < 7 ? t.dist : 0]);
    if (t.dist_pressure > 2) {
        out.Append(" +");
        append_pct(out, v01(t.dist_pressure));
    }
}

void pad_text(Str& label, Str& value, int i, const TelemetryState& t) {
    label.Clear();
    value.Clear();
    label.Append('P');
    label.AppendInt(i);
    label.Append(' ');
    if (i == 0 || i == 2) {
        label.Append("Pad mode");
        value.Append(t.pad_mode ? "DIST" : "OSC");
    } else if (i == 1) {
        label.Append("Steer");
        value.Append(t.steer_latched ? "on - tap off" : "tap/hold");
    } else if (i == 10) {
        label.Append("Attack");
        value.Append(kAttackNames[t.attack_step % 3]);
    } else if (i == 11) {
        label.Append("Decay");
        value.Append(kDecayNames[t.decay_step % 3]);
    } else if (t.pad_mode == 0) {
        label.Append("Osc pitch");
        pitch_value(value, t);
    } else {
        label.Append("Distortion");
        dist_value(value, t);
    }
}

void status_text(Str& label, Str& value, const TelemetryState& t) {
    label.Clear();
    label.Append(t.pad_mode ? "DIST" : "OSC");
    label.Append(" A ");
    label.Append(kAttackNames[t.attack_step % 3]);
    label.Append(" D ");
    label.Append(kDecayNames[t.decay_step % 3]);
    if (t.pad_mode) dist_value(value, t);
    else pitch_value(value, t);
}

const char* hold_label(uint8_t kind) { return kind == 1 ? "P10+P11 Recalibrate" : "P11 Decay reset"; }
const char* hold_note(uint8_t kind) { return kind == 1 ? "keep both held" : "back to 120 ms"; }
const char* hold_confirm(uint8_t kind) { return kind == 1 ? "pads done" : "120 ms"; }

// How long a callout stays before the status row returns. Same as IDLE_MS in
// webmanual/src/panel/oled-mini.ts.
constexpr uint32_t kIdleMs = 2200;
// Minimum gap between redraws: a full frame is ~14 ms of blocked main loop.
constexpr uint32_t kMinRedrawIntervalMs = 40;
constexpr uint32_t kConfirmFlashMs = 400;
// Pressure moves this many 7-bit steps before a held pad's callout redraws.
constexpr int kPressureStep = 3;

}  // namespace

void OledUi::Service(const TelemetryState& t, uint32_t now_ms, OledScreen& oled) {
    if (!has_last_) {
        last_ = t;
        has_last_ = true;
        idle_at_ms_ = now_ms;  // the boot status line gets its full idle time
    }
    // `last_` stays at the last drawn frame while throttled, so whatever
    // changed in between still reads as new once the window opens.
    if (static_cast<int32_t>(now_ms - next_draw_ms_) < 0) return;
    Draw(oled, t, now_ms);
}

void OledUi::Draw(OledScreen& oled, const TelemetryState& t, uint32_t now_ms) {
    const bool steer = t.steer_active;
    Str label, value;

    // 1. A hold toward a threshold owns the screen.
    if (t.hold_kind != 0) {
        const bool confirmed = t.hold_stage != 0 && (t.hold_kind != last_.hold_kind || t.hold_stage != last_.hold_stage);
        if (confirmed) {
            oled.ShowLine(hold_label(t.hold_kind), hold_confirm(t.hold_kind));
            next_draw_ms_ = now_ms + kConfirmFlashMs;
        } else if (t.hold_stage == 0) {
            oled.ShowProgress(hold_label(t.hold_kind), t.hold_progress, hold_note(t.hold_kind));
            next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
        }
        last_ = t;
        idle_at_ms_ = now_ms;
        showing_status_ = false;
        pad_callout_ = -1;
        return;
    }
    const bool hold_ended = last_.hold_kind != 0;

    bool draw = false;
    int pickup_knob = -1;
    const uint16_t new_touches = t.pads & ~last_.pads;

    if (new_touches != 0) {
        int i = 0;
        while (!((new_touches >> i) & 1)) i++;
        pad_text(label, value, i, t);
        pad_callout_ = (i >= 3 && i <= 9) ? i : -1;
        draw = true;
    } else if (t.steer_latched != last_.steer_latched) {
        label.Append("P1 Steer");
        value.Append(t.steer_latched ? "latched on" : "off");
        draw = true;
    } else if (t.steer_active != last_.steer_active) {
        label.Append("P1 Steer");
        value.Append(t.steer_active ? "momentary" : "off");
        draw = true;
    } else if (t.pad_mode != last_.pad_mode) {
        label.Append("Pad mode");
        value.Append(t.pad_mode ? "DIST" : "OSC");
        draw = true;
    } else if (t.attack_step != last_.attack_step) {
        label.Append("P10 Attack");
        value.Append(kAttackNames[t.attack_step % 3]);
        draw = true;
    } else if (t.decay_step != last_.decay_step) {
        label.Append("P11 Decay");
        value.Append(kDecayNames[t.decay_step % 3]);
        draw = true;
    } else if (t.osc_pad != last_.osc_pad || t.midi_note != last_.midi_note) {
        label.Append(t.osc_pad == 0x7F ? "MIDI note" : "Osc pitch");
        pitch_value(value, t);
        draw = true;
    } else if (t.dist != last_.dist) {
        label.Append("Distortion");
        dist_value(value, t);
        draw = true;
    } else if (t.motion != last_.motion || t.sw1 != last_.sw1) {
        label.Append("SW1 Osc motion");
        value.Append(kMotionNames[t.motion % 3]);
        draw = true;
    } else if (t.noise != last_.noise || t.sw2 != last_.sw2) {
        label.Append("SW2 Noise");
        value.Append(kNoiseNames[t.noise % 3]);
        draw = true;
    } else {
        for (int i = 0; i < 8; i++) {
            const bool armed = (t.pickup >> i) & 1;
            if (t.controls[i] != last_.controls[i]) {
                // Moved while armed = MIDI moved it (the pot is not connected).
                knob_label(label, i, armed);
                knob_value(value, i, v01(t.controls[i]));
                draw = true;
                break;
            }
            if (armed && t.pots[i] != last_.pots[i]) {
                knob_label(label, i, false);
                knob_value(value, i, v01(t.controls[i]));
                pickup_knob = i;
                draw = true;
                break;
            }
            if (!armed && ((last_.pickup >> i) & 1)) {
                // Just picked up: drop the track, show the plain value.
                knob_label(label, i, false);
                knob_value(value, i, v01(t.controls[i]));
                draw = true;
                break;
            }
        }
    }
    if (pad_callout_ >= 0 && !((t.pads >> pad_callout_) & 1)) pad_callout_ = -1;

    // A held play pad's callout follows its pressure (the bent pitch, or the
    // extra drive). Pressure moves on every frame, so it redraws in steps.
    bool keep_pressure = false;
    if (!draw && pad_callout_ >= 0) {
        const uint8_t pr = t.pad_mode ? t.dist_pressure : t.pitch_pressure;
        const uint8_t was = t.pad_mode ? last_.dist_pressure : last_.pitch_pressure;
        const int d = static_cast<int>(pr) - static_cast<int>(was);
        if (d >= kPressureStep || d <= -kPressureStep || (pr == 0 && was != 0)) {
            pad_text(label, value, pad_callout_, t);
            draw = true;
        } else {
            keep_pressure = true;  // measure the next step from the last drawn value
        }
    }

    if (draw) {
        if (pickup_knob >= 0) oled.ShowPickup(label.Cstr(), value.Cstr(), t.pots[pickup_knob], t.controls[pickup_knob]);
        else oled.ShowLine(label.Cstr(), value.Cstr(), steer);
        next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
        idle_at_ms_ = now_ms;
        showing_status_ = false;
        last_ = t;
        return;
    }

    // Nothing new: the status row, once the last callout has had its time,
    // and kept current after that. A held play pad keeps its callout.
    if (pad_callout_ >= 0) idle_at_ms_ = now_ms;
    Str sl, sv;
    status_text(sl, sv, t);
    const bool idle = hold_ended || (now_ms - idle_at_ms_) >= kIdleMs;
    const bool changed = std::strcmp(sl.Cstr(), status_label_.Cstr()) != 0
                      || std::strcmp(sv.Cstr(), status_value_.Cstr()) != 0 || steer != status_steer_;
    if (idle && (!showing_status_ || changed)) {
        oled.ShowLine(sl.Cstr(), sv.Cstr(), steer);
        next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
        showing_status_ = true;
        status_label_ = sl;
        status_value_ = sv;
        status_steer_ = steer;
    }
    const uint8_t pp = last_.pitch_pressure, dp = last_.dist_pressure;
    last_ = t;
    if (keep_pressure) {
        last_.pitch_pressure = pp;
        last_.dist_pressure = dp;
    }
}
