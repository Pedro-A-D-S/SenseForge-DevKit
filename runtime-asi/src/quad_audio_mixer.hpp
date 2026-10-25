// quad_audio_mixer.hpp — PortAudio WASAPI transport for the 4-channel
// DualSense USB audio (SF-5). SENSEFORGE_SPEC.md §2.2 / skill §3:
//   * finds the output endpoint whose name contains "Wireless Controller"
//     with maxOutputChannels >= 4;
//   * opens 48 kHz float32, 4 channels, 256-sample buffers;
//   * the real-time callback only pumps frames through MixerCore::Mix()
//     (speaker pair ch0/1, voice-coil pair ch2/3, tanh soft limiter).
// All DSP lives in MixerCore (unit-tested, hardware-free); this wrapper is
// deliberately thin — the physical validation is the SF-11 checkpoint with
// the controller plugged in.

#pragma once

#include <string>

#include "portaudio.h"

#include "mixer_core.hpp"

namespace senseforge {

// Returns the PortAudio device index of the DualSense 4-channel endpoint,
// or -1 when absent. Exposed for SenseForge Studio (SF-7) and tests.
int FindDualSenseOutputDevice(const char* name_hint);

class QuadAudioMixer {
public:
    struct Settings {
        std::string device_hint = "Wireless Controller";
        double speaker_volume = 0.75;
        double haptic_gain = 1.0;
        double ceiling = 0.92;
        unsigned long frames_per_buffer = 256;  // spec: 128-256
    };

    QuadAudioMixer() = default;
    ~QuadAudioMixer();

    QuadAudioMixer(const QuadAudioMixer&) = delete;
    QuadAudioMixer& operator=(const QuadAudioMixer&) = delete;

    // Initializes PortAudio (once per process), locates the endpoint and
    // starts the stream. On failure fills *error and leaves the mixer idle
    // (fail-closed — the game never loses audio or stability).
    bool Start(const Settings& settings, std::string* error = nullptr);
    void Stop();

    bool running() const { return stream_ != nullptr; }
    int device_index() const { return device_index_; }
    const std::string& device_name() const { return device_name_; }

    MixerCore& core() { return core_; }

private:
    static int PaCallback(const void* /*input*/, void* output,
                          unsigned long frame_count,
                          const PaStreamCallbackTimeInfo* /*time_info*/,
                          PaStreamCallbackFlags /*flags*/, void* user_data);

    MixerCore core_;
    PaStream* stream_ = nullptr;
    int device_index_ = -1;
    std::string device_name_;
    bool portaudio_initialized_ = false;
};

}  // namespace senseforge
