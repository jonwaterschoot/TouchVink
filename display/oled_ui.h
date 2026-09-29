#pragma once
// Drives the OLED from the same TelemetryState the web manual receives
// (midi/telemetry.h), after TouchPlaited's display/oled_ui. The texts are the
// ones webmanual/src/core/describe.ts produces from the same fields, so the
// screen on the panel and the one on the page read the same.
//
// What wins the screen, highest first:
//   a hold building toward a threshold (bar, then a confirm flash)
//   a pad going down
//   a state change a pad or MIDI caused (steer, pad mode, A/D step, pitch, dist)
//   a switch
//   a knob (or, for a knob behind a pickup, its pickup track)
// and kIdleMs after the last of those, the status row: pad mode, the A/D
// steps, and the pitch or distortion in play.
//
// Redraws are change-driven and throttled: each one blocks the main loop for
// the I2C transfer, and pads, knobs and MIDI all run there.

#include <cstdint>
#include "daisy_seed.h"
#include "oled_screen.h"
#include "telemetry.h"
#include "nocopy.h"

namespace touchvink {

class OledUi {
  public:
    OledUi() {}

    // Main loop, once per pass, with the snapshot Telemetry just got.
    void Service(const TelemetryState& t, uint32_t now_ms, OledScreen& oled);

  private:
    NOCOPY(OledUi)

    void Draw(OledScreen& oled, const TelemetryState& t, uint32_t now_ms);

    TelemetryState last_{};
    bool has_last_ = false;
    uint32_t next_draw_ms_ = 0;
    uint32_t idle_at_ms_ = 0;
    bool showing_status_ = false;
    int pad_callout_ = -1;  // play pad whose callout is up (it follows pressure)
    daisy::FixedCapStr<24> status_label_{};
    daisy::FixedCapStr<24> status_value_{};
    bool status_steer_ = false;
};

}  // namespace touchvink
