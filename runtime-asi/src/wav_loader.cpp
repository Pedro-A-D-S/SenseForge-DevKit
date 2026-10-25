// wav_loader.cpp — minimal RIFF parser (SF-5).

#include "wav_loader.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>

namespace senseforge {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool LoadWav(const std::filesystem::path& path, WavData& out,
             std::string* error) {
    const auto fail = [&](const char* reason) {
        if (error) {
            *error = std::string(reason) + " (" + path.string() + ")";
        }
        return false;
    };

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail("cannot open file");
    }

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return fail("not a RIFF/WAVE file");
    }

    // Walk chunks: fmt , data (skip everything else).
    std::uint16_t format = 0;
    unsigned channels = 0;
    unsigned sample_rate = 0;
    unsigned bits = 0;
    const std::uint8_t* data = nullptr;
    std::uint32_t data_size = 0;

    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const std::uint8_t* header = bytes.data() + offset;
        const std::uint32_t size = ReadU32(header + 4);
        const std::uint8_t* body = header + 8;
        if (offset + 8 + size > bytes.size()) {
            break;  // truncated chunk
        }

        if (std::memcmp(header, "fmt ", 4) == 0) {
            if (size < 16) {
                return fail("fmt chunk too small");
            }
            format = ReadU16(body);
            channels = ReadU16(body + 2);
            sample_rate = ReadU32(body + 4);
            bits = ReadU16(body + 14);
            if (format == kFormatExtensible && size >= 40) {
                // SubFormat GUID's first 2 bytes = the real format tag.
                format = ReadU16(body + 24);
            }
        } else if (std::memcmp(header, "data", 4) == 0) {
            data = body;
            data_size = size;
        }
        offset += 8 + size + (size & 1);  // chunks are word-aligned
    }

    if (data == nullptr) {
        return fail("no data chunk");
    }
    if (channels == 0 || channels > 2) {
        return fail("only mono/stereo supported");
    }
    if (sample_rate != 48000) {
        return fail("sample rate must be 48000 (no resampling)");
    }

    if (format == kFormatPcm && bits == 16) {
        const size_t frames = data_size / (2 * channels);
        out.interleaved.resize(frames * channels);
        for (size_t i = 0; i < out.interleaved.size(); ++i) {
            const std::int16_t value =
                static_cast<std::int16_t>(ReadU16(data + i * 2));
            out.interleaved[i] = static_cast<float>(value) / 32768.0f;
        }
    } else if (format == kFormatFloat && bits == 32) {
        const size_t frames = data_size / (4 * channels);
        out.interleaved.resize(frames * channels);
        std::memcpy(out.interleaved.data(), data,
                    out.interleaved.size() * sizeof(float));
    } else {
        return fail("unsupported encoding (want PCM16 or float32)");
    }

    out.channels = channels;
    out.sample_rate = sample_rate;
    return true;
}

void SplitChannels(const WavData& wav, std::vector<float>& left,
                   std::vector<float>& right) {
    left.clear();
    right.clear();
    const size_t frames = wav.interleaved.size() / wav.channels;
    left.reserve(frames);
    right.reserve(frames);
    if (wav.channels == 1) {
        for (float sample : wav.interleaved) {
            left.push_back(sample);
            right.push_back(sample);
        }
    } else {
        for (size_t frame = 0; frame < frames; ++frame) {
            left.push_back(wav.interleaved[frame * 2]);
            right.push_back(wav.interleaved[frame * 2 + 1]);
        }
    }
}

}  // namespace senseforge
