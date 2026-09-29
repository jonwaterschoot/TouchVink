#include "engine.h"

using namespace touchvink;

void Engine::Init(float sr, float* bufL, float* bufR, size_t len, daisysp::ReverbSc* verb) {
    sr_ = sr;

    osc_.Init(sr);
    motion_lfo_.Init(sr);
    motion_lfo_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    motion_lfo_.SetFreq(cfg::kMotionLfoHz);
    motion_lfo_.SetAmp(1.f);
    motion_oct_.Init(cfg::kMotionDrunkGlideSec, sr, 0.f);
    motion_shape_.Init(cfg::kMotionDrunkGlideSec, sr, cfg::kOscSawMix);
    noise_lfo_.Init(sr);
    noise_lfo_.SetWaveform(daisysp::Oscillator::WAVE_SIN);
    noise_lfo_.SetFreq(cfg::kNoiseLfoHz);
    noise_lfo_.SetAmp(0.5f);
    osc_freq_.Init(cfg::kOscGlideSec, sr, 110.f);

    exc_env_.Init(sr);
    SetAttackStep(0);
    SetDecayStep(1);

    delL_.Init(bufL, len);
    delR_.Init(bufR, len);
    delay_samps_.Init(cfg::kDelayGlideSec, sr, 0.3f * sr);

    verb_ = verb;
    verb_->Init(sr);
    verb_->SetLpFreq(cfg::kRevLpHz);
    verb_->SetFeedback(cfg::kRevFbMin);

    hpL_.Init(cfg::kLoopHpHz, sr);
    hpR_.Init(cfg::kLoopHpHz, sr);
    lpL_.Init(cfg::kLoopLpHz, sr);
    lpR_.Init(cfg::kLoopLpHz, sr);
    dcL_.Init(10.f, sr);
    dcR_.Init(10.f, sr);
    env_.Init(cfg::kEnvAttackMs, cfg::kEnvReleaseMs, sr);
    env_slow_.Init(cfg::kSteerSlowMs, cfg::kSteerSlowMs, sr);
    steer_floor_ = db2lin(cfg::kSteerFloorDb);
    hiss_ = db2lin(cfg::kHissDb);

    const float t = 0.02f;  // 20 ms zipper smoothing for knobs
    s_osc_noise_.Init(t, sr, p_.osc_noise);
    s_ext_.Init(t, sr, p_.ext_gain);
    s_ring_.Init(t, sr, p_.ring);
    s_rev_.Init(t, sr, p_.reverb);
    s_gain_.Init(t, sr, 0.f);
    s_mix_.Init(t, sr, p_.source_mix);
    s_vol_.Init(t, sr, p_.out_vol);
    s_dist_p_.Init(t, sr, 0.f);
}

void Engine::SetAttackStep(int i) { exc_env_.SetAttack(cfg::kAttackSteps[i % 3]); }
void Engine::SetDecayStep(int i) { exc_env_.SetDecay(cfg::kDecaySteps[i % 3]); }

// Things that don't need per-sample updates (every 16 samples).
void Engine::UpdateSlowParams() {
    const float rv = p_.reverb;
    verb_->SetFeedback(lerpf(cfg::kRevFbMin, cfg::kRevFbMax, rv));

    distL_.type = distR_.type = p_.dist;

    const float dms = cfg::kMinDelayMs * powf(cfg::kMaxDelayMs / cfg::kMinDelayMs, p_.delay);
    delay_target_ = dms * 0.001f * sr_;
    fb_target_ = cfg::kMaxLoopGain * powf(p_.loop_gain, 1.2f);
}

// Internal source (IN2). Returns the free-running carrier (osc/noise at the S30
// balance) and writes the AD-gated version to 'excite'.
float Engine::InternalSource(float& excite) {
    // --- motion (SW1): an offset in octaves and a sine<->saw shape for the osc
    float m_oct = 0.f, m_shape = cfg::kOscSawMix;
    if (p_.motion == OscMotion::Lfo) {
        const float l = motion_lfo_.Process();  // -1..1
        m_oct = l * cfg::kMotionLfoOct;
        m_shape = 0.5f + 0.5f * l;
    } else if (p_.motion == OscMotion::Drunk) {
        if (drunk_timer_ == 0) {
            drunk_timer_ = static_cast<uint32_t>(cfg::kMotionDrunkStepSec * sr_);
            const float r = cfg::kMotionDrunkRangeOct;
            drunk_oct_ += rng_.Bi() * cfg::kMotionDrunkStepOct;
            if (drunk_oct_ > r) drunk_oct_ = 2.f * r - drunk_oct_;   // reflect at the edges
            if (drunk_oct_ < -r) drunk_oct_ = -2.f * r - drunk_oct_;
            drunk_shape_ = clampf(drunk_shape_ + rng_.Bi() * 0.25f, 0.f, 1.f);
        }
        drunk_timer_--;
        m_oct = drunk_oct_;
        m_shape = drunk_shape_;
    }
    // smoothed so switching SW1 never clicks
    m_oct = motion_oct_.Process(m_oct);
    m_shape = motion_shape_.Process(m_shape);

    // --- oscillator (V-FUG) with pad pitch, pressure bend, steering ratio and motion
    const float target = p_.pad_freq_hz * steer_ratio_ * exp2f(p_.pitch_pressure * cfg::kPressureBendOct + m_oct);
    osc_.SetFreq(osc_freq_.Process(target));
    const float o = osc_.Process(m_shape);

    // --- noise
    const float w = rng_.Bi();
    const float pk = pink_.Process(w);
    const float br = brown_.Process(w);
    float n;
    switch (p_.noise) {
        case NoiseMode::Pink: n = pk; break;
        case NoiseMode::Brown: n = br; break;
        default: n = lerpf(pk, br, noise_lfo_.Process() + 0.5f); break;
    }
    n *= 0.6f;

    const float bal = s_osc_noise_.Process(p_.osc_noise);
    const float carrier = lerpf(o, n, bal);
    excite = carrier * exc_env_.Process();
    return carrier;
}

void Engine::Process(float inL, float inR, float& outL, float& outR) {
    if ((slow_cnt_++ & 15) == 0) UpdateSlowParams();

    // ------------------------------------------------ sources
    // The internal osc/noise runs all the time (like Vink's V-FUG) and is the
    // ring-mod carrier. The pads' AD envelope only gates how much of it is
    // *injected* into the loop (the excitation).
    float in2_excite;
    const float in2_carrier = InternalSource(in2_excite);
    const float eg = s_ext_.Process(p_.ext_gain);
    const float ext_gain = eg * eg * 4.f;  // 0..+12 dB, squared taper
    const float mix = s_mix_.Process(p_.source_mix);
    const float extL = inL * ext_gain, extR = inR * ext_gain;
    const float modL = lerpf(extL, in2_carrier, mix);   // ring-mod carrier
    const float modR = lerpf(extR, in2_carrier, mix);
    const float excL = lerpf(extL, in2_excite, mix);    // added into the loop
    const float excR = lerpf(extR, in2_excite, mix);

    // ------------------------------------------------ REC: read loop delay
    const float ds = delay_samps_.Process(delay_target_);
    const float hiss = rng_.Bi() * hiss_;
    const float dL = delL_.Read(ds) + hiss;
    const float dR = delR_.Read(ds * cfg::kDelayStereoSpread + 3.f) - hiss;

    // ------------------------------------------------ AC-MUP: loop direct <-> loop x carrier
    // Excitation is always added, so pads/ext can kick the loop at any S32 setting.
    const float r = s_ring_.Process(p_.ring);
    float xL = lerpf(dL, dL * modL * cfg::kRingMakeup, r) + excL;
    float xR = lerpf(dR, dR * modR * cfg::kRingMakeup, r) + excR;

    // ------------------------------------------------ REV
    const float rv = s_rev_.Process(p_.reverb);
    float vL, vR;
    verb_->Process(xL, xR, &vL, &vR);
    const float wet = rv * 0.8f;
    xL = lerpf(xL, vL, wet);
    xR = lerpf(xR, vR, wet);

    // tape bandwidth
    xL = lpL_.Process(hpL_.Process(xL));
    xR = lpR_.Process(hpR_.Process(xR));

    // ------------------------------------------------ V-AMM: VCA driven by inverted envelope
    const float g = 1.f / (1.f + cfg::kAgcDepth * env_.env);
    float yL = xL * g;
    float yR = xR * g;

    // ------------------------------------------------ distortion
    const float dp = s_dist_p_.Process(p_.dist_pressure);
    distL_.drive = distR_.drive = cfg::kDistBaseDrive + dp * cfg::kDistPressureDrive;
    float oL, oR;
    if (cfg::kDistInLoop) {
        yL = dcL_.Process(distL_.Process(yL));
        yR = dcR_.Process(distR_.Process(yR));
        oL = yL; oR = yR;
    } else {
        oL = dcL_.Process(distL_.Process(yL));
        oR = dcR_.Process(distR_.Process(yR));
    }

    // AMD: envelope follower on the loop output (feeds the VCA next sample)
    const float e = env_.Process(fmaxf(fabsf(yL), fabsf(yR)));

    // ------------------------------------------------ feedback write
    const float fb = s_gain_.Process(fb_target_);
    // safety clamp so a NaN or runaway never lives in the buffer
    delL_.Write(clampf(yL * fb, -2.f, 2.f));
    delR_.Write(clampf(yR * fb, -2.f, 2.f));

    // ------------------------------------------------ T-SAH steering
    // Onset detector: fires when the fast loop envelope jumps above its own slow
    // average (a "swell" in the feedback), then re-samples the osc pitch.
    const float slow = env_slow_.Process(e);
    if (steer_timer_ > 0) steer_timer_--;
    if (p_.steer) {
        const bool above = e > slow * cfg::kSteerRatio && e > steer_floor_;
        if (steer_armed_ && above && steer_timer_ == 0) {
            steer_ratio_ = exp2f(rng_.Bi() * cfg::kSteerDepthOct);
            steer_armed_ = false;
            steer_fired_ = true;
            steer_count_ = steer_count_ + 1;
            steer_timer_ = static_cast<uint32_t>(cfg::kSteerMinIntervalSec * sr_);
        } else if (!steer_armed_ && e < slow * cfg::kSteerRearm) {
            steer_armed_ = true;
        }
    } else {
        steer_ratio_ = 1.f;
        steer_armed_ = true;
    }

    // ------------------------------------------------ OUT
    const float v = s_vol_.Process(p_.out_vol);
    const float og = v * v * cfg::kOutMaxGain;
    outL = soft_tanh(oL * og);
    outR = soft_tanh(oR * og);
}
