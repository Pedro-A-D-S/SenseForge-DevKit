// mixer_core.cpp — DSP implementation (SF-5).

#include "mixer_core.hpp"

#include <algorithm>
#include <cmath>

namespace senseforge {

void MixerCore::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    one_shots_.clear();
    for (int bus = 0; bus < 2; ++bus) {
        loops_[bus] = Voice{};
        has_loop_[bus] = false;
    }
}

Voice* MixerCore::AddOneShot(Bus bus, std::vector<float> left,
                             std::vector<float> right, double gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    Voice voice;
    voice.bus = bus;
    voice.left = std::move(left);
    voice.right = std::move(right);
    voice.gain = gain;
    voice.loop = false;
    if (voice.left.empty() && voice.right.empty()) {
        return nullptr;  // nothing to play — fail-closed no-op
    }
    one_shots_.push_back(std::move(voice));
    return &one_shots_.back();
}

Voice* MixerCore::SetLoop(Bus bus, std::vector<float> left,
                          std::vector<float> right, double gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t index = static_cast<std::size_t>(bus);
    Voice& slot = loops_[index];
    slot.left = std::move(left);
    slot.right = std::move(right);
    slot.cursor = 0;
    slot.loop = true;
    slot.gain = gain;
    slot.finished = false;
    has_loop_[index] = !slot.left.empty() || !slot.right.empty();
    return has_loop_[index] ? &slot : nullptr;
}

void MixerCore::ClearLoop(Bus bus) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t index = static_cast<std::size_t>(bus);
    loops_[index] = Voice{};
    has_loop_[index] = false;
}

void MixerCore::SetLoopGain(Bus bus, double gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    loops_[static_cast<std::size_t>(bus)].gain = gain;
}

void MixerCore::SetMasterGains(double speaker, double haptics) {
    std::lock_guard<std::mutex> lock(mutex_);
    master_[static_cast<std::size_t>(Bus::Speaker)] = speaker;
    master_[static_cast<std::size_t>(Bus::Haptics)] = haptics;
}

void MixerCore::SetCeiling(double ceiling) {
    std::lock_guard<std::mutex> lock(mutex_);
    ceiling_ = std::clamp(ceiling, 0.05, 1.0);
}

void MixerCore::SetMuted(bool muted) {
    std::lock_guard<std::mutex> lock(mutex_);
    muted_ = muted;
}

void MixerCore::Mix(float* out_interleaved, std::size_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t samples = frames * 4;
    for (std::size_t i = 0; i < samples; ++i) {
        out_interleaved[i] = 0.0f;
    }

    MixBus(Bus::Speaker, out_interleaved, frames);
    MixBus(Bus::Haptics, out_interleaved, frames);

    // Soft limiter per sample: tanh saturation * ceiling — overlapping
    // effects compress instead of clipping the voice coils.
    const double ceiling = ceiling_;
    for (std::size_t i = 0; i < samples; ++i) {
        out_interleaved[i] =
            static_cast<float>(std::tanh(out_interleaved[i]) * ceiling);
    }

    // Retire finished one-shots.
    one_shots_.erase(
        std::remove_if(one_shots_.begin(), one_shots_.end(),
                       [](const Voice& voice) { return voice.finished; }),
        one_shots_.end());
}

void MixerCore::MixBus(Bus bus, float* out_interleaved, std::size_t frames) {
    const std::size_t index = static_cast<std::size_t>(bus);
    const double master = muted_ ? 0.0 : master_[index];
    const std::size_t channel_base = index * 2;  // Speaker:0, Haptics:2

    const auto advance = [&](Voice& voice) {
        if (voice.left.empty() && voice.right.empty()) {
            voice.finished = true;
            return;
        }
        const std::size_t length =
            std::max(voice.left.size(), voice.right.size());
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (voice.cursor >= length) {
                if (voice.loop) {
                    voice.cursor = 0;
                } else {
                    voice.finished = true;
                    return;
                }
            }
            const double left = voice.cursor < voice.left.size()
                                     ? voice.left[voice.cursor]
                                     : 0.0f;
            const double right = voice.cursor < voice.right.size()
                                      ? voice.right[voice.cursor]
                                      : 0.0f;
            out_interleaved[frame * 4 + channel_base] +=
                static_cast<float>(left * voice.gain * master);
            out_interleaved[frame * 4 + channel_base + 1] +=
                static_cast<float>(right * voice.gain * master);
            ++voice.cursor;
        }
        // Exactly consumed by this Mix call: done now, not on the next tick.
        if (!voice.loop && voice.cursor >= length) {
            voice.finished = true;
        }
    };

    if (has_loop_[index]) {
        advance(loops_[index]);
    }
    for (Voice& voice : one_shots_) {
        if (voice.bus == bus) {
            advance(voice);
        }
    }
}

int MixerCore::active_voice_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = static_cast<int>(one_shots_.size());
    if (has_loop_[static_cast<std::size_t>(Bus::Speaker)]) {
        ++count;
    }
    if (has_loop_[static_cast<std::size_t>(Bus::Haptics)]) {
        ++count;
    }
    return count;
}

bool MixerCore::has_loop(Bus bus) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_loop_[static_cast<std::size_t>(bus)];
}

}  // namespace senseforge
