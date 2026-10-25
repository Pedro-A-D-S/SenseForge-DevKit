// wav_loader.hpp — minimal RIFF/WAVE reader for the DSP pipeline output
// (SF-5). The haptics pipeline (SF-10) writes 48 kHz PCM — 16-bit or
// IEEE-float — as stereo pairs (`*_speaker_ch12.wav` / `*_ch34.wav`).
// Fail-closed: anything unexpected returns false with a reason.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace senseforge {

struct WavData {
    unsigned channels = 0;     // 1 or 2
    unsigned sample_rate = 0;  // must be 48000 (device rate; no resampling)
    std::vector<float> interleaved;  // normalized [-1.0, +1.0]
};

// Parses RIFF/WAVE: PCM16 (format 1), IEEE float32 (format 3) and
// WAVE_FORMAT_EXTENSIBLE (0xFFFE) carrying either subformat. Mono is kept
// as a single channel; the mixer duplicates it across the target pair.
bool LoadWav(const std::filesystem::path& path, WavData& out, std::string* error);

// Deinterleaves into per-pair channel vectors (mono duplicates to both).
void SplitChannels(const WavData& wav, std::vector<float>& left,
                   std::vector<float>& right);

}  // namespace senseforge
