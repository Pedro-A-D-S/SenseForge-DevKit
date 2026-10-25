// quad_audio_mixer.cpp — PortAudio v19.7.0 WASAPI transport (SF-5).

#include "quad_audio_mixer.hpp"

#include <cstring>

namespace senseforge {

int FindDualSenseOutputDevice(const char* name_hint) {
    for (int i = 0; i < Pa_GetDeviceCount(); ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info == nullptr || info->maxOutputChannels < 4 ||
            info->name == nullptr) {
            continue;
        }
        if (std::strstr(info->name, name_hint) != nullptr) {
            return i;
        }
    }
    return -1;
}

QuadAudioMixer::~QuadAudioMixer() {
    Stop();
}

bool QuadAudioMixer::Start(const Settings& settings, std::string* error) {
    if (stream_ != nullptr) {
        return true;  // already running
    }

    const PaError init = Pa_Initialize();
    if (init != paNoError) {
        if (error) {
            *error = std::string("Pa_Initialize: ") + Pa_GetErrorText(init);
        }
        return false;
    }
    portaudio_initialized_ = true;

    device_index_ = FindDualSenseOutputDevice(settings.device_hint.c_str());
    if (device_index_ < 0) {
        if (error) {
            *error = "no 4-channel endpoint named '" +
                     settings.device_hint + "' (is the DualSense on USB?)";
        }
        return false;
    }
    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index_);
    device_name_ = info->name != nullptr ? info->name : "";

    core_.SetMasterGains(settings.speaker_volume, settings.haptic_gain);
    core_.SetCeiling(settings.ceiling);

    const PaStreamParameters output{
        device_index_,
        4,                                   // speaker L/R + coil L/R
        paFloat32,
        0.0,                                 // suggested latency: default
        nullptr,
    };
    const PaError opened = Pa_OpenStream(
        &stream_, nullptr, &output, 48000.0, settings.frames_per_buffer,
        paClipOff, &QuadAudioMixer::PaCallback, this);
    if (opened != paNoError || stream_ == nullptr) {
        if (error) {
            *error = std::string("Pa_OpenStream: ") + Pa_GetErrorText(opened);
        }
        stream_ = nullptr;
        return false;
    }

    const PaError started = Pa_StartStream(stream_);
    if (started != paNoError) {
        if (error) {
            *error = std::string("Pa_StartStream: ") + Pa_GetErrorText(started);
        }
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void QuadAudioMixer::Stop() {
    if (stream_ != nullptr) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    if (portaudio_initialized_) {
        Pa_Terminate();
        portaudio_initialized_ = false;
    }
}

// static
int QuadAudioMixer::PaCallback(const void* /*input*/, void* output,
                               unsigned long frame_count,
                               const PaStreamCallbackTimeInfo* /*time_info*/,
                               PaStreamCallbackFlags /*flags*/,
                               void* user_data) {
    auto* mixer = static_cast<QuadAudioMixer*>(user_data);
    mixer->core_.Mix(static_cast<float*>(output), frame_count);
    return paContinue;
}

}  // namespace senseforge
