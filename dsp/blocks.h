#pragma once
// Small self-contained DSP blocks for TouchVink.
// No libDaisy dependency -> compiles on desktop for the host render test.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace touchvink {

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float db2lin(float db) { return powf(10.f, db * 0.05f); }

// 1-pole smoothing coefficient for a time constant in seconds.
inline float tau2coef(float sec, float sr) {
    if (sec <= 0.f) return 1.f;
    return 1.f - expf(-1.f / (sec * sr));
}

// Cheap rational tanh (accurate enough for saturation, safe for large x).
inline float soft_tanh(float x) {
    x = clampf(x, -3.f, 3.f);
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

// ------------------------------------------------------------------ smoothers
struct OnePole {
    float y = 0.f, a = 1.f;
    void Init(float sec, float sr, float start = 0.f) { a = tau2coef(sec, sr); y = start; }
    float Process(float x) { y += a * (x - y); return y; }
};

struct OnePoleLP {
    float y = 0.f, a = 1.f;
    void Init(float hz, float sr) { a = 1.f - expf(-2.f * float(M_PI) * hz / sr); }
    float Process(float x) { y += a * (x - y); return y; }
};

// 1-pole high-pass (also acts as DC blocker at low cutoffs).
struct OnePoleHP {
    float lp = 0.f, a = 1.f;
    void Init(float hz, float sr) { a = 1.f - expf(-2.f * float(M_PI) * hz / sr); }
    float Process(float x) { lp += a * (x - lp); return x - lp; }
};

// ------------------------------------------------------------------ noise
struct Rng {
    uint32_t s = 22222u;
    inline uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float Uni() { return (Next() >> 8) * (1.f / 16777216.f); }  // 0..1
    inline float Bi() { return Uni() * 2.f - 1.f; }                    // -1..1
};

// Paul Kellet's economy pink filter.
struct Pink {
    float b0 = 0, b1 = 0, b2 = 0;
    float Process(float w) {
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.22f;
    }
};

// Leaky integrator brown noise.
struct Brown {
    float y = 0.f;
    float Process(float w) {
        y = (y + 0.02f * w) * (1.f / 1.02f);
        return y * 3.5f;
    }
};

// ------------------------------------------------------------------ oscillator ("V-FUG")
// Sine and band-limited saw from one phase, so the shape can be blended continuously.
struct BlendOsc {
    float ph = 0.f, inc = 0.f, sr = 48000.f;
    void Init(float sample_rate) { sr = sample_rate; }
    void SetFreq(float hz) { inc = clampf(hz / sr, 0.f, 0.45f); }
    static float PolyBlep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    // saw_mix 0 = sine, 1 = saw. Output about +-0.5.
    float Process(float saw_mix) {
        const float s = sinf(2.f * float(M_PI) * ph);
        const float w = 2.f * ph - 1.f - PolyBlep(ph, inc);
        ph += inc;
        if (ph >= 1.f) ph -= 1.f;
        return 0.5f * lerpf(s, w, saw_mix);
    }
};

// ------------------------------------------------------------------ envelope follower ("AMD")
struct EnvFollower {
    float env = 0.f, att = 1.f, rel = 1.f;
    void Init(float att_ms, float rel_ms, float sr) {
        att = tau2coef(att_ms * 0.001f, sr);
        rel = tau2coef(rel_ms * 0.001f, sr);
    }
    float Process(float x) {
        x = fabsf(x);
        env += (x > env ? att : rel) * (x - env);
        return env;
    }
};

// ------------------------------------------------------------------ AD envelope (pads P10/P11)
// Linear attack, exponential decay. decay < 0 = drone (hold at peak).
struct ADEnv {
    enum Stage { Idle, Attack, Decay, Hold };
    Stage stage = Idle;
    float y = 0.f, peak = 1.f, att_inc = 1.f, dec_coef = 0.f, sr = 48000.f;
    bool drone = false;

    void Init(float sample_rate) { sr = sample_rate; }
    void SetAttack(float sec) { att_inc = 1.f / (fmaxf(sec, 0.0005f) * sr); }
    void SetDecay(float sec) {
        drone = sec < 0.f;
        if (!drone) dec_coef = tau2coef(sec * 0.3f, sr);  // ~ -60 dB at 'sec'
        else if (stage == Decay) stage = Hold;
        if (!drone && stage == Hold) stage = Decay;
    }
    void Trigger(float level) { peak = clampf(level, 0.f, 1.f); stage = Attack; }
    void Kill() { stage = Decay; drone = false; }
    float Process() {
        switch (stage) {
            case Attack:
                y += att_inc;
                if (y >= peak) { y = peak; stage = drone ? Hold : Decay; }
                break;
            case Decay:
                y -= dec_coef * y;
                if (y < 0.0001f) { y = 0.f; stage = Idle; }
                break;
            case Hold: y += 0.001f * (peak - y); break;
            case Idle: default: break;
        }
        return y;
    }
};

// ------------------------------------------------------------------ fractional delay on an external buffer ("REC")
struct LoopDelay {
    float* buf = nullptr;
    size_t len = 0, w = 0;
    void Init(float* buffer, size_t length) {
        buf = buffer; len = length; w = 0;
        for (size_t i = 0; i < len; i++) buf[i] = 0.f;
    }
    void Write(float x) { buf[w] = x; w = (w + 1 == len) ? 0 : w + 1; }
    // delay in samples, >= 1
    float Read(float d) const {
        d = clampf(d, 1.f, float(len - 3));
        const size_t di = static_cast<size_t>(d);
        const float frac = d - float(di);
        size_t r0 = (w + len - di) % len;
        size_t r1 = (r0 + len - 1) % len;
        return buf[r0] + frac * (buf[r1] - buf[r0]);
    }
};

// ------------------------------------------------------------------ distortion palette (pads P3..P9 in DIST mode)
enum class DistType : uint8_t { Clean = 0, Soft, Hard, Fold, HalfRect, FullRect, Crush, Count };

struct Distortion {
    DistType type = DistType::Clean;
    float drive = 1.f;
    float hold = 0.f;
    uint32_t cnt = 0;

    float Process(float x) {
        const float d = drive;
        switch (type) {
            case DistType::Clean: return x;
            case DistType::Soft: return soft_tanh(x * d) / soft_tanh(d);
            case DistType::Hard: return clampf(x * d, -0.6f, 0.6f) * 1.4f;
            case DistType::Fold: {
                float v = x * d * 0.8f;
                // triangle wavefolder
                v = v * 0.25f + 0.25f;
                v = v - floorf(v);
                return (fabsf(v * 4.f - 2.f) - 1.f) * 0.8f;
            }
            case DistType::HalfRect: { float v = soft_tanh(x * d); return v > 0.f ? v : v * 0.1f; }
            case DistType::FullRect: return fabsf(soft_tanh(x * d)) * 1.2f - 0.3f;  // octave-up, DC removed later
            case DistType::Crush: {
                const uint32_t step = 1 + static_cast<uint32_t>(d * 2.f);
                if (cnt++ % step == 0) {
                    const float q = 4.f + 60.f / d;
                    hold = roundf(x * q) / q;
                }
                return hold;
            }
            default: return x;
        }
    }
};

}  // namespace touchvink
