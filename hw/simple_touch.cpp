#include "simple_touch.h"
#include <cmath>

using namespace touchvink;
using namespace daisy;
using namespace daisy::seed;

// ============================================================ Knobs
void Knobs::Init(DaisySeed& hw) {
    hw_ = &hw;
    AdcChannelConfig cfg[8];
    const Pin pins[8] = {A0, A1, A2, A3, A4, A5, A6, A7};
    for (int i = 0; i < 8; i++) cfg[i].InitSingle(pins[i]);
    hw.adc.Init(cfg, 8);
    hw.adc.Start();
}

bool Knobs::Process() {
    bool moved = false;
    for (int i = 0; i < 8; i++) {
        float v = hw_->adc.GetFloat(i);
        // pots rarely reach the rails: stretch 2%..97% to 0..1
        v = (v - 0.02f) / 0.95f;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        val_[i] += 0.08f * (v - val_[i]);  // light smoothing, engine smooths again
        if (fabsf(val_[i] - last_[i]) > 0.004f) { last_[i] = val_[i]; moved = true; }
    }
    return moved;
}

// ============================================================ Switches
void Switches::Init() {
    sw1_.Init(D7, D6);
    sw2_.Init(D9, D8);
}

// ============================================================ Pads (MPR121 with pressure)
static constexpr uint8_t kAddr = 0x5A;

void Pads::Write(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    i2c_.TransmitBlocking(kAddr, b, 2, 10);
}

bool Pads::ReadBurst(uint8_t reg, uint8_t* buf, uint16_t n) {
    if (i2c_.TransmitBlocking(kAddr, &reg, 1, 10) != I2CHandle::Result::OK) return false;
    return i2c_.ReceiveBlocking(kAddr, buf, n, 10) == I2CHandle::Result::OK;
}

bool Pads::Init() {
    I2CHandle::Config c;
    c.mode = I2CHandle::Config::Mode::I2C_MASTER;
    c.periph = I2CHandle::Config::Peripheral::I2C_1;
    c.speed = I2CHandle::Config::Speed::I2C_400KHZ;
    c.pin_config.scl = Pin(PORTB, 8);
    c.pin_config.sda = Pin(PORTB, 9);
    i2c_.Init(c);

    Write(0x80, 0x63);  // soft reset
    System::Delay(5);
    Write(0x5E, 0x00);  // stop mode for config

    for (uint8_t i = 0; i < 12; i++) {
        Write(0x41 + i * 2, 6);  // touch threshold
        Write(0x42 + i * 2, 3);  // release threshold
    }
    // Baseline filters (NXP AN3891): recover fast on release, adapt slowly while
    // pressed so a held finger is not "absorbed" into the baseline.
    Write(0x2B, 0x01); Write(0x2C, 0x01); Write(0x2D, 0x0E); Write(0x2E, 0x00);  // rising
    Write(0x2F, 0x01); Write(0x30, 0x01); Write(0x31, 0x10); Write(0x32, 0x04);  // falling
    Write(0x33, 0x00); Write(0x34, 0x00); Write(0x35, 0x00);                    // touched
    Write(0x5B, 0x11);  // debounce 1/1
    Write(0x5C, 0xD0);  // CONFIG1: 34 first-filter samples, 16 uA
    Write(0x5D, 0x51);  // CONFIG2: 1 us charge, 10 second-filter samples, 2 ms period
    Write(0x7D, 200); Write(0x7E, 130); Write(0x7F, 180);  // auto-config limits (3.3 V)
    Write(0x7B, 0xCB); Write(0x7C, 0x00);
    Write(0x5E, 0x8C);  // run: 12 electrodes, baseline tracking on
    System::Delay(80);

    uint8_t probe[2];
    last_ms_ = System::GetNow();
    return ReadBurst(0x00, probe, 2);
}

void Pads::Recalibrate() {
    Write(0x5E, 0x00);
    System::Delay(5);
    Write(0x5E, 0x8C);
    System::Delay(80);
    state_ = 0;
    pressure_.fill(0.f);
}

void Pads::Process() {
    rise_ = fall_ = 0;
    const uint32_t now = System::GetNow();
    const uint32_t dt = now - last_ms_;
    last_ms_ = now;

    // 0x00-0x01 touch status, 0x04-0x1B filtered data (10 bit), 0x1E-0x29 baseline (>>2)
    uint8_t raw[42];
    if (!ReadBurst(0x00, raw, 42)) return;
    const uint16_t hw_state = ((raw[1] & 0x0F) << 8) | raw[0];

    for (int i = 0; i < kNumPads; i++) {
        const uint16_t m = 1u << i;
        const uint16_t filt = ((raw[4 + i * 2 + 1] & 0x03) << 8) | raw[4 + i * 2];
        const uint16_t base = uint16_t(raw[0x1E + i]) << 2;
        int32_t delta = int32_t(base) - int32_t(filt);
        if (delta < 0) delta = 0;

        const bool touched = (hw_state & m) && delta >= 3;
        const bool was = state_ & m;
        const float full = max_delta[i] > 40.f ? max_delta[i] - 5.f : 35.f;
        float norm = (delta - 5) / full;
        norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);

        if (touched && !was) {
            state_ |= m;
            strike_[i] = 3;  // collect the peak over a few reads for velocity
            peak_[i] = delta;
            held_ms_[i] = 0;
        } else if (!touched && was) {
            state_ &= ~m;
            fall_ |= m;
            if (strike_[i] > 0) {  // very short tap: report it anyway
                strike_[i] = 0;
                velocity_[i] = 0.3f;
                rise_ |= m;
            }
        }

        if (state_ & m) {
            held_ms_[i] += dt;
            if (strike_[i] > 0) {
                if (delta > peak_[i]) peak_[i] = delta;
                if (--strike_[i] == 0) {
                    float pn = (peak_[i] - 5) / full;
                    pn = pn < 0.f ? 0.f : (pn > 1.f ? 1.f : pn);
                    velocity_[i] = 0.35f * pn + 0.65f * sqrtf(pn);
                    rise_ |= m;
                }
            }
            pressure_[i] += 0.4f * (norm * norm - pressure_[i]);
        } else {
            pressure_[i] *= 0.6f;
            if (pressure_[i] < 0.01f) pressure_[i] = 0.f;
        }
    }
}
