#pragma once
// Panel-state telemetry for the web manual (webmanual/), sent as SysEx over
// USB MIDI. Same design as TouchPlaited's midi/telemetry: full-state frames,
// rate-limited, with a heartbeat so the page can join mid-stream.
//
//   F0 7D 54 56 <ver> <type> <payload…> F7     mfr 0x7D (educational), "TV"
//
// The byte layout lives in one place per side and they must move together:
// Telemetry::SendState/SendMeter below, and webmanual/src/core/protocol.ts.

#include <cstdint>

namespace touchvink {

class MidiIO;

// Everything the web manual and the OLED show, already in wire format
// (7-bit values). The OLED UI (display/oled_ui.cpp) reads this same struct,
// so the screen and the page cannot disagree.
struct TelemetryState {
    uint16_t pads;         // bit i = pad Pi touched (hardware or MIDI)
    uint8_t controls[8];   // S30..S37 value in effect, 0..127 (the CC value while a
                           // pot is behind a pickup, the pot otherwise)
    uint8_t pots[8];       // S30..S37 physical pot position, 0..127
    uint8_t pickup;        // bit i = S3(0+i) is behind a pickup: a MIDI CC set it
                           // and the pot does nothing until it crosses controls[i]
    uint8_t sw1, sw2;      // physical lever, panel order: SW1 0 left..2 right, SW2 0 top..2 bottom
    uint8_t motion, noise; // what is in effect (a CC can override a lever until it moves)
    uint8_t led;           // user LED, 0 or 127
    uint8_t pad_mode;      // 0 OSC, 1 DIST
    bool steer_latched;    // P1 tapped on
    bool steer_active;     // latched, or P1 held as momentary
    uint8_t attack_step;   // 0..2
    uint8_t decay_step;    // 0..2 (2 = drone)
    uint8_t osc_pad;       // pitch picked by P3..P9 in OSC mode, 0..6; 0x7F = MIDI note
    uint8_t midi_note;     // ch 1 note in effect when osc_pad = 0x7F
    uint8_t dist;          // distortion type 0..6
    uint8_t pitch_pressure;// 0..127, what bends the osc (latched in drone)
    uint8_t dist_pressure; // 0..127, what adds drive (latched in drone)
    uint8_t hold_kind;     // 0 none, 1 recalibrate (P10+P11), 2 decay reset (P11 long)
    uint8_t hold_progress; // 0..127 toward the threshold
    uint8_t hold_stage;    // 0 building, 1 fired (stays 1 while still held)

    // Fast-moving, sent in their own METER frame so they don't drive STATE.
    uint8_t excite;        // excitation envelope 0..127
    uint8_t loop_db;       // loop envelope, 0 = -60 dBFS .. 127 = 0 dBFS
    uint8_t steer_count;   // +1 per steering event, wraps at 128
    uint32_t osc_centihz;  // osc pitch in 1/100 Hz (incl. steering, pressure, motion)
};

// Emits frames over USB MIDI, rate-limited:
//   STATE  on change, at most every 33 ms; heartbeat every 500 ms
//   METER  at most every 50 ms while anything in it moves
//   HELLO  at boot and when the page asks (REQUEST), followed by a STATE
// Main loop only.
class Telemetry {
  public:
    void Service(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);
    // The page sent REQUEST: answer with HELLO + STATE + METER on the next Service.
    void RequestFull() { full_ = true; }
    // Parses one received SysEx body (no F0/F7); true if it was ours.
    bool HandleSysex(const uint8_t* data, uint32_t len);

  private:
    void SendState(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);
    void SendMeter(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);
    void SendHello(MidiIO& midi);

    TelemetryState last_state_{};
    TelemetryState last_meter_{};
    uint32_t state_ms_ = 0;
    uint32_t meter_ms_ = 0;
    bool full_ = true;  // first Service sends everything
};

}  // namespace touchvink
