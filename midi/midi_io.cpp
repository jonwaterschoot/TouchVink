#include "midi_io.h"
#include "daisy_seed.h"

using namespace daisy;

namespace touchvink {

// File-scope transports: each handler carries a 256-event FIFO and there is
// exactly one MidiIO in the app.
#ifdef TRS_MIDI
static MidiUartHandler uart_midi;
#endif
#ifdef USB_MIDI
static MidiUsbHandler usb_midi;
#endif

void MidiIO::Init() {
#ifdef TRS_MIDI
    // Defaults are the TRS mod's wiring: USART1, D14 = RX, D13 = TX.
    MidiUartHandler::Config uart_cfg;
    uart_midi.Init(uart_cfg);
    uart_midi.StartReceive();
#endif
#ifdef USB_MIDI
    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    // With no host attached every TX fails; each retry burns 100 us of the
    // main loop for nothing.
    usb_cfg.transport_config.tx_retry_count = 1;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();
#endif
}

static void dispatch(MidiEvent ev, const MidiHandlers& h) {
    switch (ev.type) {
        case NoteOn: {  // vel-0 NoteOns already arrive as NoteOff (parser)
            NoteOnEvent e = ev.AsNoteOn();
            h.note_on(static_cast<uint8_t>(e.channel), e.note, e.velocity);
            break;
        }
        case NoteOff: {
            NoteOffEvent e = ev.AsNoteOff();
            h.note_off(static_cast<uint8_t>(e.channel), e.note);
            break;
        }
        case PolyphonicKeyPressure: {
            PolyphonicKeyPressureEvent e = ev.AsPolyphonicKeyPressure();
            h.poly_pressure(static_cast<uint8_t>(e.channel), e.note, e.pressure);
            break;
        }
        case ChannelPressure: {
            ChannelPressureEvent e = ev.AsChannelPressure();
            h.channel_pressure(static_cast<uint8_t>(e.channel), e.pressure);
            break;
        }
        case ControlChange: {
            ControlChangeEvent e = ev.AsControlChange();
            h.control_change(static_cast<uint8_t>(e.channel), e.control_number, e.value);
            break;
        }
        case ChannelMode:  // CCs 120-127 arrive as this type, not ControlChange
            if (ev.cm_type == AllSoundOff || ev.cm_type == AllNotesOff) h.all_notes_off();
            break;
        case SystemCommon:
            if (ev.sc_type == SystemExclusive) h.sysex(ev.sysex_data, ev.sysex_message_len);
            break;
        default: break;
    }
}

void MidiIO::Service(const MidiHandlers& handlers) {
#ifdef TRS_MIDI
    uart_midi.Listen();  // restarts RX after a UART overrun error
    while (uart_midi.HasEvents()) dispatch(uart_midi.PopEvent(), handlers);
#endif
#ifdef USB_MIDI
    usb_midi.Listen();
    while (usb_midi.HasEvents()) dispatch(usb_midi.PopEvent(), handlers);
#endif

    // Bounded drain: UART TX blocks ~1 ms per 3-byte message. Whatever is
    // left goes out on the next pass.
    int budget = 8;
    while (q_tail_ != q_head_ && budget--) {
        OutMsg& m = queue_[q_tail_ & (kQueueSize - 1)];
#ifdef TRS_MIDI
        uart_midi.SendMessage(m.b, m.len);
#endif
#ifdef USB_MIDI
        usb_midi.SendMessage(m.b, m.len);
#endif
        q_tail_++;
    }
}

void MidiIO::SendSysexUsb(const uint8_t* bytes, size_t len) {
#ifdef USB_MIDI
    // MidiUsbTransport::Tx packetizes SysEx itself (CIN 0x04 / 0x05-0x07).
    usb_midi.SendMessage(const_cast<uint8_t*>(bytes), len);
#else
    (void)bytes;
    (void)len;
#endif
}

void MidiIO::push(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t len) {
    if (q_head_ - q_tail_ >= kQueueSize) return;  // full: drop, never block
    OutMsg& m = queue_[q_head_ & (kQueueSize - 1)];
    m.len = len;
    m.b[0] = b0;
    m.b[1] = b1;
    m.b[2] = b2;
    q_head_++;
}

void MidiIO::SendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    push(0x90 | (channel & 0x0F), note & 0x7F, velocity & 0x7F, 3);
}

void MidiIO::SendNoteOff(uint8_t channel, uint8_t note) {
    push(0x80 | (channel & 0x0F), note & 0x7F, 0, 3);
}

void MidiIO::SendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure) {
    push(0xA0 | (channel & 0x0F), note & 0x7F, pressure & 0x7F, 3);
}

void MidiIO::SendCC(uint8_t channel, uint8_t control, uint8_t value) {
    push(0xB0 | (channel & 0x0F), control & 0x7F, value & 0x7F, 3);
}

}  // namespace touchvink
