#pragma once
// Hardware layer for the Synthux Simple Touch (Daisy Seed + MPR121).
//
// Pin map (same as AudreyTouch / TouchString):
//   Knobs  S30..S37  -> A0..A7   (S36 / S37 are the two faders)
//   SW1 (left)       -> D7 / D6
//   SW2 (right)      -> D9 / D8
//   MPR121           -> I2C1, SCL PB8, SDA PB9, addr 0x5A
//
// Pressure sensing: the MPR121 exposes per-electrode "filtered data" and a
// "baseline". A finger lowers the filtered value; baseline - filtered (delta)
// grows with contact area, i.e. with how hard you press. Approach credited to
// vincentltm/TouchString (github.com/vincentltm/TouchString).

#include "daisy_seed.h"
#include <array>
#include <cstdint>

namespace touchvink {

class Knobs {
  public:
    void Init(daisy::DaisySeed& hw);
    // Call at control rate (~1 kHz). Returns true if any knob moved.
    bool Process();
    float Get(int i) const { return val_[i]; }  // 0..1, CW = 1

  private:
    daisy::DaisySeed* hw_ = nullptr;
    std::array<float, 8> val_{};
    std::array<float, 8> last_{};
};

class Switches {
  public:
    void Init();
    int SW1() { return Map(sw1_.Read()); }  // 1 = up, 2 = middle, 3 = down
    int SW2() { return Map(sw2_.Read()); }

  private:
    static int Map(int pos) {
        return pos == daisy::Switch3::POS_UP ? 1 : (pos == daisy::Switch3::POS_CENTER ? 2 : 3);
    }
    daisy::Switch3 sw1_, sw2_;
};

class Pads {
  public:
    static constexpr int kNumPads = 12;

    bool Init();           // false if the MPR121 does not answer
    void Process();        // call at ~250 Hz - 1 kHz
    void Recalibrate();

    bool Touched(int pad) const { return state_ & (1u << pad); }
    bool JustTouched(int pad) const { return rise_ & (1u << pad); }
    bool JustReleased(int pad) const { return fall_ & (1u << pad); }
    float Pressure(int pad) const { return pressure_[pad]; }  // 0..1 while held
    float Velocity(int pad) const { return velocity_[pad]; }  // 0..1 latched at touch
    uint32_t HeldMs(int pad) const { return held_ms_[pad]; }

    // Per-pad full-scale delta (pads differ in size / trace length). Tune on your unit.
    std::array<float, kNumPads> max_delta = {375, 375, 375, 375, 460, 375, 375, 375, 425, 425, 375, 375};

  private:
    void Write(uint8_t reg, uint8_t val);
    bool ReadBurst(uint8_t reg, uint8_t* buf, uint16_t n);

    daisy::I2CHandle i2c_;
    uint16_t state_ = 0, rise_ = 0, fall_ = 0;
    std::array<float, kNumPads> pressure_{};
    std::array<float, kNumPads> velocity_{};
    std::array<uint8_t, kNumPads> strike_{};
    std::array<int32_t, kNumPads> peak_{};
    std::array<uint32_t, kNumPads> held_ms_{};
    uint32_t last_ms_ = 0;
};

}  // namespace touchvink
