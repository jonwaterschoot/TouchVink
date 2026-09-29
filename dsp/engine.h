#pragma once
// TouchVink engine — Jaap Vink style feedback loop for the Synthux Simple Touch.
//
//   IN1 ext stereo ──(S31 gain)──┐
//                                ├─ S36 SOURCE MIX ─┐
//   IN2 osc/noise ─(S30 bal)─ AD ┘                  │
//                                                   ▼
//        ┌──── REC (loop delay, S35) ──► AC-MUP (ring / add, S32) ──► REV (S33)
//        │                                                              │
//        │                                            tape tone filters ▼
//        └──◄── × S34 loop gain ◄── DIST ◄── V-AMM (VCA, gain = 1/(1+k·env)) ◄┘
//                                    │                ▲
//                                    │     AMD env follower + inverter ("compressor")
//                                    ▼
//                                   OUT (S37)
//
// Steering (T-SAH): when the env follower crosses a threshold, the osc
// pitch is re-sampled at random around the selected pad pitch.

#include "blocks.h"
#include "config.h"
#include "daisysp.h"

namespace touchvink {

// SW1: how the osc moves by itself. Lfo = slow sweep, Drunk = random walk (pitch and shape).
enum class OscMotion : uint8_t { Lfo = 0, Steady, Drunk };
enum class NoiseMode : uint8_t { Pink = 0, Drift, Brown };  // Drift = pink<->brown moved by an LFO

// All values normalized 0..1 unless noted. Written from the control thread,
// read in the audio callback (single floats -> no tearing on Cortex-M7).
struct Params {
    float osc_noise = 0.f;     // S30  0 = osc only, 1 = noise only
    float ext_gain = 0.5f;     // S31
    float ring = 0.f;          // S32  0 = add (excite), 1 = pure ring modulation
    float reverb = 0.2f;       // S33
    float loop_gain = 0.5f;    // S34  VCA -> feedback
    float delay = 0.3f;        // S35
    float source_mix = 0.5f;   // S36  0 = ext only, 1 = internal only (fader is inverted in the firmware)
    float out_vol = 0.6f;      // S37

    OscMotion motion = OscMotion::Steady; // SW1
    NoiseMode noise = NoiseMode::Pink; // SW2

    float pad_freq_hz = 110.f;  // chosen by pads P3..P9 in OSC mode
    float pitch_pressure = 0.f; // 0..1 aftertouch bend
    DistType dist = DistType::Clean;
    float dist_pressure = 0.f;  // 0..1
    bool steer = false;         // P1
};

class Engine {
  public:
    // delay buffers and reverb are passed in so the firmware can place them in SDRAM.
    void Init(float sr, float* bufL, float* bufR, size_t len, daisysp::ReverbSc* verb);

    // Called from the control thread.
    Params& params() { return p_; }
    void TriggerExcite(float velocity) { exc_env_.Trigger(0.35f + 0.65f * velocity); }
    void SetAttackStep(int i);
    void SetDecayStep(int i);
    float ExciteLevel() const { return exc_env_.y; }
    float LoopLevel() const { return env_.env; }
    bool SteerFired() { bool f = steer_fired_; steer_fired_ = false; return f; }
    // Running count of steering events, for telemetry (SteerFired is the LED's).
    uint32_t SteerCount() const { return steer_count_; }
    // Osc pitch right now: pad or note, pressure bend, motion and steering.
    float OscHz() const { return osc_freq_.y; }

    void Process(float inL, float inR, float& outL, float& outR);

  private:
    float InternalSource(float& excite);
    void UpdateSlowParams();

    Params p_;
    float sr_ = 48000.f;

    // sources (IN2)
    BlendOsc osc_;
    daisysp::Oscillator noise_lfo_;
    daisysp::Oscillator motion_lfo_;
    float drunk_oct_ = 0.f, drunk_shape_ = cfg::kOscSawMix;
    uint32_t drunk_timer_ = 0;
    OnePole motion_oct_, motion_shape_;
    Rng rng_;
    Pink pink_;
    Brown brown_;
    OnePole osc_freq_;
    ADEnv exc_env_;
    float steer_ratio_ = 1.f;
    bool steer_armed_ = true;
    bool steer_fired_ = false;
    volatile uint32_t steer_count_ = 0;
    uint32_t steer_timer_ = 0;

    // loop
    LoopDelay delL_, delR_;
    OnePole delay_samps_;
    daisysp::ReverbSc* verb_ = nullptr;
    OnePoleHP hpL_, hpR_;
    OnePoleLP lpL_, lpR_;
    OnePoleHP dcL_, dcR_;  // after distortion
    EnvFollower env_;
    EnvFollower env_slow_;
    float steer_floor_ = 0.f;
    Distortion distL_, distR_;
    float hiss_ = 0.f;

    // smoothed controls
    OnePole s_osc_noise_, s_ext_, s_ring_, s_rev_, s_gain_, s_mix_, s_vol_, s_dist_p_;
    uint32_t slow_cnt_ = 0;
    float delay_target_ = 4800.f;
    float fb_target_ = 0.f;
};

}  // namespace touchvink
