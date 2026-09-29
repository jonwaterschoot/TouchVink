// Host render test: runs the TouchVink engine on your computer (no Daisy needed)
// and writes WAV files + prints level statistics, so loop stability and sound
// can be checked before flashing.
//
//   make -C host && ./host/render   (from the repo root; writes host/out/*.wav)

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include "engine.h"

using namespace touchvink;

static float bufL[cfg::kMaxDelaySamples];
static float bufR[cfg::kMaxDelaySamples];
static daisysp::ReverbSc verb;

static void write_wav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("cannot write %s\n", path.c_str()); return; }
    const uint32_t n = L.size(), data = n * 4, riff = 36 + data;
    const uint16_t ch = 2, bits = 16, fmt = 1, ba = 4;
    const uint32_t fmtlen = 16, br = sr * 4, usr = sr;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&usr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) {
        int16_t s[2] = {(int16_t)(clampf(L[i], -1, 1) * 32767), (int16_t)(clampf(R[i], -1, 1) * 32767)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

struct Scenario {
    const char* name;
    const char* what;
    float seconds;
    std::function<void(Engine&)> setup;
    // per-block hook: t = seconds, lets a scenario "play" pads/knobs over time
    std::function<void(Engine&, float, float&, float&)> tick;
};

int main() {
    const int sr = (int)cfg::kSampleRate;
    Rng in_rng;

    std::vector<Scenario> scenarios = {
        {"01_self_excite", "No input, internal source off. Loop gain high, full ring: blooms from tape hiss like Vink.", 20.f,
         [](Engine& e) { auto& p = e.params(); p.source_mix = 1.f; p.ring = 0.0f; p.loop_gain = 0.85f; p.delay = 0.55f; p.reverb = 0.5f; p.out_vol = 0.7f; },
         [](Engine& e, float t, float&, float&) { e.params().ring = t < 8.f ? 0.f : 0.6f; }},

        {"02_osc_pads", "Sine osc played on pads with AD envelope, medium feedback, half ring, reverb.", 16.f,
         [](Engine& e) { auto& p = e.params(); p.source_mix = 1.f; p.osc_noise = 0.1f; p.ring = 0.5f; p.loop_gain = 0.7f; p.delay = 0.45f; p.reverb = 0.4f; e.SetAttackStep(0); e.SetDecayStep(1); },
         [](Engine& e, float t, float&, float&) {
             static int last = -1; int step = (int)(t / 1.5f);
             if (step != last && t < 12.f) { last = step; e.params().pad_freq_hz = cfg::kPadFreqs[3 + step % 4]; e.TriggerExcite(0.8f); }
         }},

        {"03_drone_steer", "Saw drone (decay step 3) + noise, steering on: env peaks resample the osc pitch.", 20.f,
         [](Engine& e) { auto& p = e.params(); p.shape = OscShape::Saw; p.source_mix = 1.f; p.osc_noise = 0.3f; p.ring = 0.7f; p.loop_gain = 0.9f; p.delay = 0.35f; p.reverb = 0.6f; p.pad_freq_hz = 110.f; p.steer = true; e.SetDecayStep(2); e.TriggerExcite(0.7f); },
         nullptr},

        {"04_ext_dist_scream", "Ext input bursts, max loop gain, hard distortion pressed in: worst case for stability.", 15.f,
         [](Engine& e) { auto& p = e.params(); p.source_mix = 0.f; p.ext_gain = 0.8f; p.ring = 0.3f; p.loop_gain = 1.0f; p.delay = 0.2f; p.reverb = 0.9f; p.dist = DistType::Hard; p.out_vol = 0.8f; },
         [&in_rng](Engine& e, float t, float& l, float& r) {
             const float ph = fmodf(t, 0.5f);
             const float amp = ph < 0.05f ? 0.8f : 0.f;
             l = in_rng.Bi() * amp; r = in_rng.Bi() * amp;
             e.params().dist_pressure = 0.5f + 0.5f * sinf(t * 0.7f);
         }},

        {"05_drift_noise_fold", "Drifting pink/brown noise, pure ring with a 5 Hz osc chopping the loop, fold distortion.", 16.f,
         [](Engine& e) { auto& p = e.params(); p.noise = NoiseMode::Drift; p.source_mix = 1.f; p.osc_noise = 0.5f; p.pad_freq_hz = 5.f; p.ring = 0.85f; p.loop_gain = 0.95f; p.delay = 0.7f; p.reverb = 0.5f; p.dist = DistType::Fold; p.dist_pressure = 0.2f; e.SetAttackStep(2); e.SetDecayStep(2); e.TriggerExcite(1.f); },
         nullptr},
    };

    bool all_ok = true;
    for (auto& sc : scenarios) {
        Engine eng;
        eng.Init(cfg::kSampleRate, bufL, bufR, cfg::kMaxDelaySamples, &verb);
        sc.setup(eng);
        const size_t n = (size_t)(sc.seconds * sr);
        std::vector<float> L(n), R(n);
        float peak = 0.f; bool nan = false;
        std::vector<float> rms_sec((size_t)sc.seconds + 1, 0.f);
        float inl = 0.f, inr = 0.f;
        int steer = 0;
        for (size_t i = 0; i < n; i++) {
            const float t = (float)i / sr;
            if (i % cfg::kBlockSize == 0 && sc.tick) sc.tick(eng, t, inl, inr);
            eng.Process(inl, inr, L[i], R[i]);
            if (eng.SteerFired()) steer++;
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) nan = true;
            peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
            rms_sec[(size_t)t] += L[i] * L[i];
        }
        printf("\n%s — %s\n  peak %.2f  nan %s  steer-events %d\n  dBFS per second: ", sc.name, sc.what, peak, nan ? "YES" : "no", steer);
        for (size_t s = 0; s < (size_t)sc.seconds; s++) printf("%.0f ", 10.f * log10f(rms_sec[s] / sr + 1e-12f));
        printf("\n");
        if (nan) all_ok = false;
        write_wav(std::string("host/out/") + sc.name + ".wav", L, R, sr);
    }
    printf("\n%s\n", all_ok ? "ALL OK (no NaN)" : "FAILURES");
    return all_ok ? 0 : 1;
}
