// test_quad_audio.cpp — endpoint discovery and fail-closed Start (SF-5).
// Hardware-independent: with no DualSense USB attached, Start must fail
// gracefully with a reason; with one attached we only verify discovery
// (opening an exclusive stream from tests would fight the audio session —
// the real stream validation is the SF-11 physical checkpoint).

#include "test_common.hpp"

#include <string>

#include "portaudio.h"

#include "quad_audio_mixer.hpp"

using senseforge::FindDualSenseOutputDevice;
using senseforge::QuadAudioMixer;

int main() {
    assert(Pa_Initialize() == paNoError);

    const int device = FindDualSenseOutputDevice("Wireless Controller");
    if (device < 0) {
        // No 4-channel DualSense endpoint: Start is fail-closed.
        QuadAudioMixer mixer;
        std::string error;
        assert(!mixer.Start(QuadAudioMixer::Settings{}, &error));
        assert(!error.empty());
        assert(!mixer.running());
        assert(mixer.device_index() < 0);
    } else {
        // DualSense present: discovery only (no stream in the test process).
        assert(device >= 0);
    }

    // Hint that matches nothing, even with devices present.
    assert(FindDualSenseOutputDevice("__no_such_device_hint__") < 0);

    Pa_Terminate();
    return 0;
}
