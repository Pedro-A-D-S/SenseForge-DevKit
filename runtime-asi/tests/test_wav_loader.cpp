// test_wav_loader.cpp — RIFF parsing round-trips (SF-5): PCM16 stereo and
// float32 mono fixtures written by the test itself, plus fail-closed paths.

#include "test_common.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "wav_loader.hpp"

using namespace senseforge;

namespace {

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void AppendChunk(std::vector<std::uint8_t>& bytes, const char* id,
                 const std::vector<std::uint8_t>& body) {
    bytes.insert(bytes.end(), id, id + 4);
    AppendU32(bytes, static_cast<std::uint32_t>(body.size()));
    bytes.insert(bytes.end(), body.begin(), body.end());
    if (body.size() & 1) {
        bytes.push_back(0);  // word alignment
    }
}

std::filesystem::path WriteFile(const std::string& name,
                                const std::vector<std::uint8_t>& bytes) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

// Builds a minimal RIFF/WAVE: fmt + data.
std::vector<std::uint8_t> BuildWav(std::uint16_t format, unsigned channels,
                                    unsigned rate, unsigned bits,
                                    const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> fmt;
    AppendU16(fmt, format);
    AppendU16(fmt, static_cast<std::uint16_t>(channels));
    AppendU32(fmt, rate);
    AppendU32(fmt, rate * channels * bits / 8);      // byte rate
    AppendU16(fmt, static_cast<std::uint16_t>(channels * bits / 8));  // align
    AppendU16(fmt, static_cast<std::uint16_t>(bits));

    std::vector<std::uint8_t> riff;
    const char header[] = {'R', 'I', 'F', 'F'};
    riff.insert(riff.end(), header, header + 4);
    AppendU32(riff, 0);  // patched below
    const char wave[] = {'W', 'A', 'V', 'E'};
    riff.insert(riff.end(), wave, wave + 4);
    AppendChunk(riff, "fmt ", fmt);
    AppendChunk(riff, "data", data);
    const std::uint32_t size =
        static_cast<std::uint32_t>(riff.size() - 8);
    std::memcpy(riff.data() + 4, &size, 4);
    return riff;
}

}  // namespace

int main() {
    // PCM16 stereo, 48 kHz: L=16384 (0.5), R=-16384 (-0.5).
    {
        std::vector<std::uint8_t> data;
        AppendU16(data, 16384);
        AppendU16(data, static_cast<std::uint16_t>(-16384));
        AppendU16(data, 0);
        AppendU16(data, 0);
        const auto path = WriteFile("sf_test_pcm16.wav",
                                    BuildWav(1, 2, 48000, 16, data));

        WavData wav;
        std::string error;
        assert(LoadWav(path, wav, &error));
        assert(error.empty());
        assert(wav.channels == 2);
        assert(wav.sample_rate == 48000);
        assert(wav.interleaved.size() == 4);
        assert(std::fabs(wav.interleaved[0] - 0.5f) < 1e-3);
        assert(std::fabs(wav.interleaved[1] + 0.5f) < 1e-3);
        assert(wav.interleaved[2] == 0.0f);

        std::vector<float> left, right;
        SplitChannels(wav, left, right);
        assert(left.size() == 2 && right.size() == 2);
        assert(std::fabs(left[0] - 0.5f) < 1e-3);
        assert(std::fabs(right[0] + 0.5f) < 1e-3);
    }

    // Float32 mono, 48 kHz: 0.25.
    {
        const float sample = 0.25f;
        std::vector<std::uint8_t> data(sizeof(float));
        std::memcpy(data.data(), &sample, sizeof(float));
        const auto path = WriteFile("sf_test_f32.wav",
                                    BuildWav(3, 1, 48000, 32, data));

        WavData wav;
        std::string error;
        assert(LoadWav(path, wav, &error));
        assert(wav.channels == 1);
        assert(std::fabs(wav.interleaved[0] - 0.25f) < 1e-6);

        std::vector<float> left, right;
        SplitChannels(wav, left, right);
        assert(left.size() == 1 && right.size() == 1);
        assert(std::fabs(right[0] - 0.25f) < 1e-6);  // mono duplicated
    }

    // Fail-closed: garbage, wrong rate, unsupported bit depth.
    {
        WavData wav;
        std::string error;
        const auto garbage = WriteFile("sf_test_garbage.wav",
                                       {'n', 'o', 't', 'a', 'w', 'a', 'v'});
        assert(!LoadWav(garbage, wav, &error));
        assert(!error.empty());

        error.clear();
        const auto wrong_rate = WriteFile(
            "sf_test_44k.wav", BuildWav(1, 2, 44100, 16, {0, 0, 0, 0}));
        assert(!LoadWav(wrong_rate, wav, &error));
        assert(!error.empty());

        error.clear();
        const auto wrong_bits = WriteFile(
            "sf_test_24bit.wav", BuildWav(1, 2, 48000, 24, {0, 0, 0}));
        assert(!LoadWav(wrong_bits, wav, &error));
        assert(!error.empty());
    }

    return 0;
}
