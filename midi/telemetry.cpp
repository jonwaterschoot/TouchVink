#include "telemetry.h"
#include "midi_io.h"

namespace touchvink {

namespace {

constexpr uint8_t kMfr = 0x7D, kId0 = 0x54, kId1 = 0x56;  // "TV"
constexpr uint8_t kVersion = 1;
constexpr uint8_t kFwMajor = 0, kFwMinor = 2;

constexpr uint8_t kFrameState = 0x01;
constexpr uint8_t kFrameHello = 0x03;
constexpr uint8_t kFrameMeter = 0x06;
constexpr uint8_t kFrameRequest = 0x7E;  // host -> device

constexpr uint32_t kStateMinIntervalMs = 33;  // ~30 Hz on change
constexpr uint32_t kHeartbeatMs = 500;
constexpr uint32_t kMeterMinIntervalMs = 50;

bool StateChanged(const TelemetryState& a, const TelemetryState& b) {
    if (a.pads != b.pads || a.pickup != b.pickup) return true;
    if (a.sw1 != b.sw1 || a.sw2 != b.sw2 || a.motion != b.motion || a.noise != b.noise) return true;
    if (a.led != b.led || a.pad_mode != b.pad_mode) return true;
    if (a.steer_latched != b.steer_latched || a.steer_active != b.steer_active) return true;
    if (a.attack_step != b.attack_step || a.decay_step != b.decay_step) return true;
    if (a.osc_pad != b.osc_pad || a.midi_note != b.midi_note || a.dist != b.dist) return true;
    if (a.pitch_pressure != b.pitch_pressure || a.dist_pressure != b.dist_pressure) return true;
    if (a.hold_kind != b.hold_kind || a.hold_progress != b.hold_progress) return true;
    if (a.hold_stage != b.hold_stage) return true;
    for (int i = 0; i < 8; i++) {
        if (a.controls[i] != b.controls[i]) return true;
        // a pot only matters while it is behind a pickup
        if (((a.pickup >> i) & 1) && a.pots[i] != b.pots[i]) return true;
    }
    return false;
}

bool MeterChanged(const TelemetryState& a, const TelemetryState& b) {
    return a.excite != b.excite || a.loop_db != b.loop_db || a.steer_count != b.steer_count
        || a.osc_centihz != b.osc_centihz;
}

void Header(uint8_t* f, uint8_t type) {
    f[0] = 0xF0; f[1] = kMfr; f[2] = kId0; f[3] = kId1; f[4] = kVersion; f[5] = type;
}

}  // namespace

bool Telemetry::HandleSysex(const uint8_t* d, uint32_t len) {
    // body: 7D 54 56 <ver> <type>
    if (len < 5 || d[0] != kMfr || d[1] != kId0 || d[2] != kId1) return false;
    if (d[4] == kFrameRequest) full_ = true;
    return true;
}

void Telemetry::Service(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    if (full_) {
        full_ = false;
        SendHello(midi);
        SendState(s, now_ms, midi);
        SendMeter(s, now_ms, midi);
        return;
    }
    const uint32_t state_age = now_ms - state_ms_;
    if (state_age >= kHeartbeatMs || (StateChanged(s, last_state_) && state_age >= kStateMinIntervalMs))
        SendState(s, now_ms, midi);
    if (MeterChanged(s, last_meter_) && now_ms - meter_ms_ >= kMeterMinIntervalMs)
        SendMeter(s, now_ms, midi);
}

void Telemetry::SendState(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    // Payload offsets (protocol.ts has the same table):
    //  0-1 pads lo7/hi5 · 2-9 controls · 10-17 pots · 18-19 pickup lo7/hi1
    //  20 sw1 · 21 sw2 · 22 motion · 23 noise · 24 led · 25 flags
    //  26 attack · 27 decay · 28 osc pad · 29 midi note · 30 dist
    //  31 pitch pressure · 32 dist pressure · 33 hold kind · 34 progress · 35 stage
    uint8_t f[6 + 36 + 1];
    Header(f, kFrameState);
    uint8_t* p = f + 6;
    p[0] = s.pads & 0x7F;
    p[1] = (s.pads >> 7) & 0x1F;
    for (int i = 0; i < 8; i++) p[2 + i] = s.controls[i] & 0x7F;
    for (int i = 0; i < 8; i++) p[10 + i] = s.pots[i] & 0x7F;
    p[18] = s.pickup & 0x7F;
    p[19] = (s.pickup >> 7) & 0x01;
    p[20] = s.sw1;
    p[21] = s.sw2;
    p[22] = s.motion;
    p[23] = s.noise;
    p[24] = s.led & 0x7F;
    // bit0 DIST mode, bit1 steer latched, bit2 steer active
    p[25] = (s.pad_mode & 1) | (s.steer_latched ? 0x02 : 0) | (s.steer_active ? 0x04 : 0);
    p[26] = s.attack_step;
    p[27] = s.decay_step;
    p[28] = s.osc_pad & 0x7F;
    p[29] = s.midi_note & 0x7F;
    p[30] = s.dist;
    p[31] = s.pitch_pressure & 0x7F;
    p[32] = s.dist_pressure & 0x7F;
    p[33] = s.hold_kind;
    p[34] = s.hold_progress & 0x7F;
    p[35] = s.hold_stage & 0x7F;
    f[sizeof(f) - 1] = 0xF7;
    midi.SendSysexUsb(f, sizeof(f));
    last_state_ = s;
    state_ms_ = now_ms;
}

void Telemetry::SendMeter(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    // 0 excite · 1 loop dB · 2 steer count · 3-5 osc centi-Hz, 21 bits, low 7 first
    uint8_t f[6 + 6 + 1];
    Header(f, kFrameMeter);
    uint8_t* p = f + 6;
    p[0] = s.excite & 0x7F;
    p[1] = s.loop_db & 0x7F;
    p[2] = s.steer_count & 0x7F;
    p[3] = s.osc_centihz & 0x7F;
    p[4] = (s.osc_centihz >> 7) & 0x7F;
    p[5] = (s.osc_centihz >> 14) & 0x7F;
    f[sizeof(f) - 1] = 0xF7;
    midi.SendSysexUsb(f, sizeof(f));
    last_meter_ = s;
    meter_ms_ = now_ms;
}

void Telemetry::SendHello(MidiIO& midi) {
    uint8_t f[6 + 3 + 1];
    Header(f, kFrameHello);
    f[6] = kFwMajor;
    f[7] = kFwMinor;
    f[8] = 0;  // feature bits, reserved
    f[9] = 0xF7;
    midi.SendSysexUsb(f, sizeof(f));
}

}  // namespace touchvink
