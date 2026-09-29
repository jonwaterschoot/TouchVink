#pragma once
// MIDI I/O, adapted from TouchPlaited's midi/ module
// (github.com/jonwaterschoot/TouchPlaited).
//
// Two transports:
//   - USB device MIDI on the Seed's built-in port, when built with -DUSB_MIDI
//     (the default; the Makefile drops it for DEBUG = 1, since serial logging
//     needs the same USB port).
//   - TRS MIDI on USART1 (D13 TX / D14 RX, 31250 baud), only with -DTRS_MIDI.
//     Off by default, unlike TouchPlaited: the pinned libDaisy still has the
//     HAL_UART_ErrorCallback null write that TouchPlaited bisected, where a
//     floating MIDI input's framing noise stopped the OLED, the pads and USB.
//     Unmodded boards have nothing on D14, so a started UART is all risk.
//
// Events from both transports go through one set of handlers. Everything runs
// in the main loop: TouchVink's pads, knobs and UI all live there, so unlike
// TouchPlaited nothing here needs an IRQ-off section.

#include <cstddef>
#include <cstdint>

namespace touchvink {

struct MidiHandlers {
    void (*note_on)(uint8_t channel, uint8_t note, uint8_t velocity);
    void (*note_off)(uint8_t channel, uint8_t note);
    void (*poly_pressure)(uint8_t channel, uint8_t note, uint8_t pressure);
    void (*channel_pressure)(uint8_t channel, uint8_t pressure);
    void (*control_change)(uint8_t channel, uint8_t control, uint8_t value);
    void (*all_notes_off)();  // CC120 All Sound Off / CC123 All Notes Off
    // SysEx body without the F0 / F7 framing.
    void (*sysex)(const uint8_t* data, size_t len);
};

class MidiIO {
  public:
    // Call once after hw.Init().
    void Init();

    // Main loop: pops received events into the handlers, then drains the
    // outgoing queue (bounded, so a burst never stalls the control loop).
    void Service(const MidiHandlers& handlers);

    // A complete SysEx frame (F0…F7), USB only: telemetry is for the host,
    // and at 31250 baud a frame would block the loop for ~10 ms. Sent now,
    // not queued. No-op without -DUSB_MIDI.
    void SendSysexUsb(const uint8_t* bytes, size_t len);

    // Queued for both transports.
    void SendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void SendNoteOff(uint8_t channel, uint8_t note);
    void SendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure);
    void SendCC(uint8_t channel, uint8_t control, uint8_t value);

  private:
    void push(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t len);

    struct OutMsg {
        uint8_t len;
        uint8_t b[3];
    };
    static constexpr uint32_t kQueueSize = 64;  // power of two
    OutMsg queue_[kQueueSize];
    uint32_t q_head_ = 0;
    uint32_t q_tail_ = 0;
};

}  // namespace touchvink
