#pragma once
// 128x32 SSD1306 on I2C1, D11 (SCL) / D12 (SDA): the bus the MPR121 pads
// already use, at another address (0x3C vs 0x5A), so it needs only power and
// the two data lines. Optional: with no screen fitted every transfer just
// NACKs and the instrument runs as before.
//
// From TouchPlaited's display/oled_screen, trimmed to the screens TouchVink
// uses. The web manual's emulator (webmanual/src/panel/oled-mini.ts) draws
// the same screens with the same fonts and geometry; change both together.
//
// Two rows: a small label (Font_6x8, uppercased, truncated) and a value that
// never truncates. It steps down Font_11x18 -> Font_7x10 -> Font_6x8 until it
// fits.

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "oled_dirty_driver.h"
#include "nocopy.h"

namespace touchvink {

class OledScreen {
  public:
    OledScreen() {}

    void Init();

    // One label + one value, replacing the whole screen. `steer` draws the
    // steering marker (a small diamond, far right of the label row): it is on
    // in every screen while steering runs, since that changes what the loop
    // does whatever you are looking at.
    void ShowLine(const char* label, const char* value, bool steer = false);

    // Four rows of Font_6x8, the whole screen. Only the first 4 of `n` are drawn.
    static constexpr int kListRows = 32 / 8;
    void ShowList(const char* const* rows, int n);

    // Label row, then an outlined bar filled progress/127, then a note row
    // saying what the threshold will do (`note` may be null).
    void ShowProgress(const char* label, uint8_t progress, const char* note);

    // A knob behind a pickup: `value` is the one in effect (set by MIDI), and a
    // track underneath carries a post at `target` and a block at `pot`, both
    // 0..127. Turn the block onto the post and the pot takes over.
    void ShowPickup(const char* label, const char* value, uint8_t pot, uint8_t target);

    void Clear();

    // Pixel access for the boot animation (display/oled_boot.cpp).
    void BeginFrame();
    void SetPixel(uint8_t x, uint8_t y, bool on);
    void EndFrame();

  private:
    NOCOPY(OledScreen)

    daisy::OledDisplay<SSD1306DirtyDriver> display_;
};

}  // namespace touchvink
