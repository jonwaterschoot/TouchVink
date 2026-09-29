#include "oled_screen.h"
#include <algorithm>
#include <cctype>
#include <cstring>

using namespace touchvink;
using namespace daisy;

namespace {
constexpr size_t kBufLen = 22;  // 21 chars (Font_6x8 across 128 px) + NUL

// Steering marker: a 7 px diamond in the label row's last cell.
constexpr uint8_t kSteerCx = 124, kSteerCy = 3;
constexpr size_t kLabelCharsWithSteer = 19;  // 114 px, clear of the diamond

// ShowPickup: Font_7x10 value at y 12..21, track at y 24..31.
constexpr uint8_t kPickupValueY = 12;

// Font_6x8 only has ASCII 32-126, and WriteString gives up on the whole
// string at the first other byte, so callers pass plain ASCII.
void copy_upper(char* buf, const char* s, size_t budget) {
    const size_t len = std::min({strlen(s), kBufLen - 1, budget});
    std::memcpy(buf, s, len);
    buf[len] = '\0';
    for (char* p = buf; *p; ++p) *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
}

void draw_label(OledDisplay<SSD1306DirtyDriver>& d, const char* label, size_t budget) {
    char buf[kBufLen];
    copy_upper(buf, label, budget);
    d.SetCursor(1, 0);
    d.WriteString(buf, Font_6x8, true);
}
}  // namespace

void OledScreen::Init() {
    OledDisplay<SSD1306DirtyDriver>::Config cfg;
    // The pads brought I2C1 up at 400 kHz; whichever Init runs last sets the
    // clock, and the driver's own default is 1 MHz, which the MPR121 is not
    // rated for. Address 0x3C, I2C_1 and D11/D12 are already the defaults.
    cfg.driver_config.transport_config.i2c_config.speed = I2CHandle::Config::Speed::I2C_400KHZ;
    display_.Init(cfg);
    Clear();
}

void OledScreen::Clear() {
    display_.Fill(false);
    display_.Update();
}

void OledScreen::ShowLine(const char* label, const char* value, bool steer) {
    display_.Fill(false);

    size_t budget = kBufLen - 1;
    if (steer) {
        budget = kLabelCharsWithSteer;
        for (int dy = -3; dy <= 3; dy++) {
            const int w = 3 - (dy < 0 ? -dy : dy);
            display_.DrawLine(kSteerCx - w, kSteerCy + dy, kSteerCx + w, kSteerCy + dy, true);
        }
    }
    draw_label(display_, label, budget);

    if (value != nullptr && *value != '\0') {
        // Largest font whose character count fits, never truncating.
        const size_t len = strlen(value);
        const FontDef* font = &Font_6x8;
        if (len <= 128u / Font_11x18.FontWidth) font = &Font_11x18;
        else if (len <= 128u / Font_7x10.FontWidth) font = &Font_7x10;
        char buf[kBufLen];
        const size_t n = std::min({len, static_cast<size_t>(128u / font->FontWidth), kBufLen - 1});
        std::memcpy(buf, value, n);
        buf[n] = '\0';
        display_.SetCursor(1, static_cast<uint8_t>(32 - font->FontHeight));
        display_.WriteString(buf, *font, true);
    }
    display_.Update();
}

void OledScreen::ShowList(const char* const* rows, int n) {
    display_.Fill(false);
    const int shown = std::min(n, kListRows);
    for (int i = 0; i < shown; i++) {
        char buf[kBufLen];
        copy_upper(buf, rows[i], kBufLen - 1);
        display_.SetCursor(1, static_cast<uint8_t>(i * Font_6x8.FontHeight));
        display_.WriteString(buf, Font_6x8, true);
    }
    display_.Update();
}

void OledScreen::ShowProgress(const char* label, uint8_t progress, const char* note) {
    display_.Fill(false);
    draw_label(display_, label, kBufLen - 1);

    constexpr uint8_t kX1 = 1, kY1 = 12, kX2 = 126, kY2 = 21;
    display_.DrawRect(kX1, kY1, kX2, kY2, true, false);
    constexpr uint8_t kFillX1 = kX1 + 2, kFillY1 = kY1 + 2, kFillY2 = kY2 - 2;
    constexpr uint8_t kFillMaxW = kX2 - 2 - kFillX1;
    const uint8_t w = static_cast<uint8_t>((static_cast<uint32_t>(kFillMaxW) * (progress & 0x7F)) / 127u);
    if (w > 0) display_.DrawRect(kFillX1, kFillY1, static_cast<uint8_t>(kFillX1 + w - 1), kFillY2, true, true);

    if (note != nullptr && *note != '\0') {
        char buf[kBufLen];
        copy_upper(buf, note, kBufLen - 1);
        display_.SetCursor(1, static_cast<uint8_t>(32 - Font_6x8.FontHeight));
        display_.WriteString(buf, Font_6x8, true);
    }
    display_.Update();
}

void OledScreen::ShowPickup(const char* label, const char* value, uint8_t pot, uint8_t target) {
    display_.Fill(false);
    draw_label(display_, label, kBufLen - 1);

    if (value != nullptr && *value != '\0') {
        char buf[kBufLen];
        const size_t n = std::min({strlen(value), static_cast<size_t>(128u / Font_7x10.FontWidth), kBufLen - 1});
        std::memcpy(buf, value, n);
        buf[n] = '\0';
        display_.SetCursor(1, kPickupValueY);
        display_.WriteString(buf, Font_7x10, true);
    }

    constexpr uint8_t kX0 = 1, kX1 = 126, kSpan = kX1 - kX0;
    auto track_x = [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(kX0 + (static_cast<uint32_t>(v & 0x7F) * kSpan) / 127u);
    };
    display_.DrawLine(kX0, 30, kX1, 30, true);
    const uint8_t tx = track_x(target);  // where the pot has to get to
    display_.DrawRect(tx, 24, static_cast<uint8_t>(tx + 1), 31, true, true);
    const uint8_t px = track_x(pot);     // where it is now
    const uint8_t pl = px >= kX0 + 2 ? static_cast<uint8_t>(px - 2) : kX0;
    const uint8_t pr = px <= kX1 - 2 ? static_cast<uint8_t>(px + 2) : kX1;
    display_.DrawRect(pl, 27, pr, 29, true, true);

    display_.Update();
}

void OledScreen::BeginFrame() { display_.Fill(false); }
void OledScreen::SetPixel(uint8_t x, uint8_t y, bool on) { display_.DrawPixel(x, y, on); }
void OledScreen::EndFrame() { display_.Update(); }
