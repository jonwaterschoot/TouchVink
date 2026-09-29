#include "oled_boot.h"
#include "daisy_seed.h"
#include "util/oled_fonts.h"
#include <cmath>

using namespace touchvink;
using namespace daisy;

namespace {

// "Vink" one font size above "Touch" (Font_6x8 vs Font_7x10), on a shared
// bottom edge, the pair centred as one block.
constexpr char kWord1[] = "Touch";
constexpr char kWord2[] = "Vink";
constexpr int kWord1Len = sizeof(kWord1) - 1;
constexpr int kWord2Len = sizeof(kWord2) - 1;
constexpr int kTextLen = kWord1Len + kWord2Len;

constexpr uint8_t kFont1W = 6, kFont1H = 8;
constexpr uint8_t kFont2W = 7, kFont2H = 10;
constexpr int kWord1PxW = kWord1Len * kFont1W;                         // 30
constexpr int kWord2PxW = kWord2Len * kFont2W;                         // 28
constexpr uint8_t kX0 = (128 - (kWord1PxW + kWord2PxW)) / 2;           // 35
constexpr uint8_t kWord2Y0 = (32 - kFont2H) / 2;                       // 11
constexpr uint8_t kBaseline = kWord2Y0 + kFont2H;                      // 21
constexpr uint8_t kWord1Y0 = kBaseline - kFont1H;                      // 13

constexpr uint32_t kLetterStepMs = 35;
constexpr uint32_t kHoldMs = 1000;
constexpr uint32_t kMessageHoldMs = 1500;
constexpr uint32_t kFrameDelayMs = 10;
constexpr int kMaxParticles = 384;  // "TouchVink" lights ~130 pixels
constexpr int kScatterFrames = 52;

// Slow LED blink while loading, ticked from every wait below.
constexpr uint32_t kBlinkPeriodMs = 300;
struct SlowBlink {
    void (*set_led)(bool);
    uint32_t acc_ms = 0;
    bool state = false;
    void Tick(uint32_t dt_ms) {
        acc_ms += dt_ms;
        if (acc_ms >= kBlinkPeriodMs) {
            acc_ms -= kBlinkPeriodMs;
            state = !state;
            set_led(state);
        }
    }
};

void WaitBlinking(uint32_t ms, SlowBlink& blink) {
    constexpr uint32_t kStepMs = 20;
    while (ms > 0) {
        const uint32_t step = ms < kStepMs ? ms : kStepMs;
        System::Delay(step);
        blink.Tick(step);
        ms -= step;
    }
}

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 1) {}
    float Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.f;
    }
    float Range(float lo, float hi) { return lo + Next() * (hi - lo); }
};

struct Particle {
    float x, y, vx, vy;
    int life;
};

struct CharSlot {
    char ch;
    const FontDef* font;
    uint8_t x0, y0;
};

CharSlot SlotAt(int c) {
    if (c < kWord1Len) return {kWord1[c], &Font_6x8, static_cast<uint8_t>(kX0 + c * kFont1W), kWord1Y0};
    const int i = c - kWord1Len;
    return {kWord2[i], &Font_7x10, static_cast<uint8_t>(kX0 + kWord1PxW + i * kFont2W), kWord2Y0};
}

// Same bit layout OneBitGraphicsDisplayImpl::WriteChar reads (MSB-first rows).
bool GlyphBit(const FontDef& font, char ch, int row, int col) {
    if (ch < 32 || ch > 126) return false;
    const uint16_t bits = font.data[(ch - 32) * font.FontHeight + row];
    return (bits << col) & 0x8000;
}

}  // namespace

void OledBoot::Run(OledScreen& oled, uint32_t seed, const char* status, void (*set_led)(bool)) {
    Rng rng(seed ^ 0x9E3779B9u);
    SlowBlink blink{set_led};
    set_led(false);

    // Materialize, one letter per step.
    oled.BeginFrame();
    for (int c = 0; c < kTextLen; ++c) {
        const CharSlot s = SlotAt(c);
        for (int row = 0; row < s.font->FontHeight; ++row)
            for (int col = 0; col < s.font->FontWidth; ++col)
                if (GlyphBit(*s.font, s.ch, row, col)) oled.SetPixel(s.x0 + col, s.y0 + row, true);
        oled.EndFrame();
        WaitBlinking(kLetterStepMs, blink);
    }
    WaitBlinking(kHoldMs, blink);

    // One particle per lit pixel, flung outward from the word's centre.
    static Particle particles[kMaxParticles];
    int n = 0;
    const float cx = kX0 + (kWord1PxW + kWord2PxW) * 0.5f;
    const float cy = (kWord2Y0 + kBaseline) * 0.5f;
    for (int c = 0; c < kTextLen && n < kMaxParticles; ++c) {
        const CharSlot s = SlotAt(c);
        for (int row = 0; row < s.font->FontHeight && n < kMaxParticles; ++row) {
            for (int col = 0; col < s.font->FontWidth && n < kMaxParticles; ++col) {
                if (!GlyphBit(*s.font, s.ch, row, col)) continue;
                Particle& p = particles[n++];
                p.x = static_cast<float>(s.x0 + col);
                p.y = static_cast<float>(s.y0 + row);
                const float dx = p.x - cx, dy = p.y - cy;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.5f) len = 0.5f;
                p.vx = dx / len * rng.Range(0.2f, 0.65f) + rng.Range(-0.125f, 0.125f);
                p.vy = dy / len * rng.Range(0.2f, 0.65f) - 0.1f + rng.Range(-0.1f, 0.1f);
                p.life = static_cast<int>(rng.Range(28.f, static_cast<float>(kScatterFrames)));
            }
        }
    }

    // Scatter with a little gravity until everything has faded or left.
    for (int frame = 0; frame < kScatterFrames; ++frame) {
        oled.BeginFrame();
        for (int i = 0; i < n; ++i) {
            Particle& p = particles[i];
            if (p.life <= 0) continue;
            p.x += p.vx;
            p.y += p.vy;
            p.vy += 0.02f;
            --p.life;
            if (p.x < 0.f || p.x >= 128.f || p.y < 0.f || p.y >= 32.f) {
                p.life = 0;
                continue;
            }
            oled.SetPixel(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), true);
        }
        oled.EndFrame();
        WaitBlinking(kFrameDelayMs, blink);
    }

    // Ready: one quick flash, then the status line.
    set_led(true);
    System::Delay(80);
    set_led(false);
    oled.ShowLine("TouchVink", status);
    System::Delay(kMessageHoldMs);
}
