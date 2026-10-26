// test_mixer_core.cpp — the DSP contract, hardware-free (SF-5):
// routing per bus, tanh soft limiter, gains, mute, voice lifecycle, loops.

#include "test_common.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "mixer_core.hpp"

using namespace senseforge;

namespace {

std::vector<float> Constant(float value, size_t count) {
    return std::vector<float>(count, value);
}

}  // namespace

int main() {
    // ---- SynthesizeTone (SF-12b speaker blips) ----------------------------
    {
        const auto tone = SynthesizeTone(880, 100, 0.5, 0);
        assert(tone.size() == 4800);  // 100ms @ 48kHz
        float peak = 0.0f;
        for (float v : tone) peak = std::max(peak, std::fabs(v));
        assert(peak > 0.3f && peak <= 0.5f + 1e-4f);        // peak ~ gain
        assert(std::fabs(tone.back()) < 0.01f);              // decayed out
        assert(std::fabs(tone.front()) < 0.05f);             // soft attack

        assert(SynthesizeTone(0, 100, 0.5, 0).empty());      // no tone
        assert(SynthesizeTone(880, 5, 0.5, 0).size() == 960);   // min 20ms
        assert(SynthesizeTone(880, 9000, 0.5, 0).size() == 48000);  // max 1s
        const auto loud = SynthesizeTone(880, 100, 9.0, 1320);
        for (float v : loud) assert(std::fabs(v) <= 1.0f);   // gain clamped
    }

    // ---- Routing: speaker voices feed only channels 0/1 -------------------
    {
        MixerCore mixer;
        mixer.SetMasterGains(1.0, 1.0);
        mixer.SetCeiling(0.92);
        mixer.AddOneShot(Bus::Speaker, Constant(0.5f, 480),
                         Constant(0.25f, 480), 1.0);

        std::vector<float> out(480 * 4, -99.0f);
        mixer.Mix(out.data(), 480);

        // Speaker pair carries tanh(0.5)*0.92 ≈ 0.4289 (left) and
        // tanh(0.25)*0.92 ≈ 0.2223 (right); coils stay silent.
        assert(std::fabs(out[0] - std::tanh(0.5) * 0.92) < 1e-3);
        assert(std::fabs(out[1] - std::tanh(0.25) * 0.92) < 1e-3);
        assert(out[2] == 0.0f && out[3] == 0.0f);
        assert(mixer.active_voice_count() == 0);  // one-shot consumed
    }

    // ---- Routing: haptics voices feed only channels 2/3 ------------------
    {
        MixerCore mixer;
        mixer.AddOneShot(Bus::Haptics, Constant(1.0f, 16), Constant(1.0f, 16),
                         1.0);
        std::vector<float> out(16 * 4, -99.0f);
        mixer.Mix(out.data(), 16);
        assert(out[0] == 0.0f && out[1] == 0.0f);
        assert(std::fabs(out[2] - std::tanh(1.0) * 0.92) < 1e-3);
        assert(std::fabs(out[3] - std::tanh(1.0) * 0.92) < 1e-3);
    }

    // ---- Soft limiter: overlapping loud voices never exceed ceiling ------
    {
        MixerCore mixer;
        for (int i = 0; i < 24; ++i) {  // 24 voices of amplitude 1.0
            mixer.AddOneShot(Bus::Haptics, Constant(1.0f, 64),
                             Constant(1.0f, 64), 1.0);
        }
        std::vector<float> out(64 * 4);
        mixer.Mix(out.data(), 64);
        for (float sample : out) {
            assert(std::fabs(sample) <= 0.92 + 1e-6);  // no clipping, ever
        }
        // Saturated towards the ceiling by tanh.
        assert(out[2] > 0.91 && out[3] > 0.91);
    }

    // ---- Master gains and mute -------------------------------------------
    {
        MixerCore mixer;
        mixer.SetMasterGains(0.5, 0.25);
        mixer.AddOneShot(Bus::Speaker, Constant(0.5f, 8), Constant(0.5f, 8),
                         1.0);
        mixer.AddOneShot(Bus::Haptics, Constant(0.5f, 8), Constant(0.5f, 8),
                         1.0);
        std::vector<float> out(8 * 4);
        mixer.Mix(out.data(), 8);
        assert(std::fabs(out[0] - std::tanh(0.25) * 0.92) < 1e-3);
        assert(std::fabs(out[2] - std::tanh(0.125) * 0.92) < 1e-3);

        mixer.SetMuted(true);
        mixer.AddOneShot(Bus::Speaker, Constant(0.5f, 8), Constant(0.5f, 8),
                         1.0);
        std::vector<float> muted(8 * 4, -99.0f);
        mixer.Mix(muted.data(), 8);
        for (float sample : muted) {
            assert(sample == 0.0f);
        }
    }

    // ---- Loop lifecycle: wraps, live gain, clear --------------------------
    {
        MixerCore mixer;
        mixer.SetLoop(Bus::Haptics, Constant(0.5f, 10), Constant(0.5f, 10),
                      1.0);
        assert(mixer.has_loop(Bus::Haptics));

        std::vector<float> out(25 * 4, 0.0f);
        mixer.Mix(out.data(), 25);
        // 25 frames over a 10-sample loop: every frame must have sound.
        for (size_t frame = 0; frame < 25; ++frame) {
            assert(out[frame * 4 + 2] > 0.4f);
        }

        // Live gain (e.g. bow tension binding) — silence without voices.
        mixer.SetLoopGain(Bus::Haptics, 0.0);
        std::vector<float> quiet(10 * 4, -99.0f);
        mixer.Mix(quiet.data(), 10);
        for (float sample : quiet) {
            assert(sample == 0.0f);
        }

        mixer.SetLoopGain(Bus::Haptics, 1.0);
        mixer.ClearLoop(Bus::Haptics);
        assert(!mixer.has_loop(Bus::Haptics));
    }

    return 0;
}
