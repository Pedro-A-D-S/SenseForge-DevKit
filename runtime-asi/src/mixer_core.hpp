// mixer_core.hpp — pure-DSP 4-channel mixer with per-bus gains and the
// Soft Limiter (SF-5). Hardware-free by design so the whole contract is
// unit-testable; the PortAudio callback only pumps frames through Mix().
//
// Routing (SENSEFORGE_SPEC.md §2.2):
//   Bus::Speaker  -> device channels 0/1 (controller speaker)  * speaker_volume
//   Bus::Haptics  -> device channels 2/3 (L/R voice-coil actuators)
//                    * custom_haptic_gain
//   Per sample:  out = tanh(mix) * soft_limiter_ceiling
// Overlapping effects saturate smoothly instead of clipping the coils.

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace senseforge {

enum class Bus { Speaker, Haptics };

struct Voice {
    std::vector<float> left;
    std::vector<float> right;
    std::size_t cursor = 0;
    bool loop = false;
    double gain = 1.0;
    bool finished = false;
    Bus bus = Bus::Speaker;  // which pair of device channels this voice feeds
};

// SF-12b: mono blip for the controller speaker. Sine at `frequency_hz` (plus an
// optional second partial at `second_hz`) with a 2ms attack and an
// exponential decay to ~-48dB at the end. Out-of-range parameters are clamped
// (frequency 100..8000, duration 20..1000ms, gain 0..1); frequency 0 yields an
// empty buffer. Peak is `gain`.
std::vector<float> SynthesizeTone(int frequency_hz, int duration_ms,
                                  double gain, int second_hz,
                                  int sample_rate = 48000);

class MixerCore {
public:
    void Reset();

    // One-shot voice (e.g. parry impact, hoof approach). Retired when done.
    Voice* AddOneShot(Bus bus, std::vector<float> left,
                      std::vector<float> right, double gain);

    // Loop voice per bus (e.g. bow string creak on Haptics): replaces the
    // previous loop on that bus. Gain can be updated live via SetLoopGain.
    Voice* SetLoop(Bus bus, std::vector<float> left,
                   std::vector<float> right, double gain);
    void ClearLoop(Bus bus);
    void SetLoopGain(Bus bus, double gain);

    void SetMasterGains(double speaker, double haptics);
    void SetCeiling(double ceiling);
    void SetMuted(bool muted);

    // Mixes `frames` interleaved 4-channel frames into `out` (4*frames
    // floats): [0]=speaker L, [1]=speaker R, [2]=L-coil, [3]=R-coil.
    void Mix(float* out_interleaved, std::size_t frames);

    int active_voice_count() const;
    bool has_loop(Bus bus) const;

private:
    void MixBus(Bus bus, float* out_interleaved, std::size_t frames);

    mutable std::mutex mutex_;
    std::vector<Voice> one_shots_;
    Voice loops_[2] = {};         // index by static_cast<size_t>(Bus)
    bool has_loop_[2] = {false, false};
    double master_[2] = {1.0, 1.0};
    double ceiling_ = 0.92;
    bool muted_ = false;
};

}  // namespace senseforge
