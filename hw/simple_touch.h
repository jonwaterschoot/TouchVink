#pragma once
// Hardware layer for the Synthux Simple Touch (Daisy Seed + MPR121).
//
// Pin map (same as AudreyTouch / TouchString):
//   Knobs  S30..S37  -> A0..A7   (S36 / S37 are the two faders)
//   SW1 (left lever, left/right)  -> D9 / D8   (board pins S9 / S10)
//   SW2 (right lever, up/down)    -> D7 / D6   (board pins S7 / S8)
//   Same as TouchPlaited, checked on hardware.
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
    // Positions in panel order, as the faceplate reads:
    // SW1 1 = left, 2 = middle, 3 = right; SW2 1 = top, 2 = middle, 3 = bottom.
    // The left lever's "up" contact is its right end (TouchPlaited's
    // sw1_panel_pos). If a lever reads mirrored on your unit, flip it here.
    int SW1() {
        const int pos = sw1_.Read();
        return pos == daisy::Switch3::POS_UP ? 3 : (pos == daisy::Switch3::POS_CENTER ? 2 : 1);
    }
    int SW2() {
        const int pos = sw2_.Read();
        return pos == daisy::Switch3::POS_UP ? 1 : (pos == daisy::Switch3::POS_CENTER ? 2 : 3);
    }

  private:
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
    uint16_t State() const { return state_; }
    // Held by a received MIDI note, so MIDI out never echoes it back.
    bool VirtTouched(int pad) const { return virt_ & (1u << pad); }

    // MIDI touches: a pad held by a received note behaves exactly like a
    // finger on it, merged with the real sensor on the next Process().
    void SetVirtual(int pad, bool on, float velocity = 0.7f);
    void SetVirtualPressure(int pad, float pressure);
    void ClearVirtual() { virt_ = 0; }

    // Per-pad full-scale delta (pads differ in size / trace length). Tune on your unit.
    std::array<float, kNumPads> max_delta = {375, 375, 375, 375, 460, 375, 375, 375, 425, 425, 375, 375};

  private:
    void Write(uint8_t reg, uint8_t val);
    bool ReadBurst(uint8_t reg, uint8_t* buf, uint16_t n);

    daisy::I2CHandle i2c_;
    uint16_t state_ = 0, rise_ = 0, fall_ = 0;
    uint16_t virt_ = 0;
    std::array<float, kNumPads> virt_velocity_{};
    std::array<float, kNumPads> virt_pressure_{};
    std::array<float, kNumPads> pressure_{};
    std::array<float, kNumPads> velocity_{};
    std::array<uint8_t, kNumPads> strike_{};
    std::array<int32_t, kNumPads> peak_{};
    std::array<uint32_t, kNumPads> held_ms_{};
    uint32_t last_ms_ = 0;
};

}  // namespace touchvink
