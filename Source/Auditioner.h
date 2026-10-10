#pragma once

#include "ToneSequence.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <mutex>

namespace va
{
/** Plays the comparison tones on its own audio output (not through the audio
    path of the plugin, so the vocal pass-through is never touched). Opens the
    device the first time it is used and closes it with the editor. On Windows
    it asks for the shared "Windows Audio" device so it can sit next to an ASIO
    driver Cubase is using. */
class Auditioner final : private juce::AudioIODeviceCallback
{
public:
    Auditioner() = default;
    ~Auditioner() override { manager.removeAudioCallback (this); manager.closeAudioDevice(); }

    /** Returns an error message, or empty when the sound started. */
    juce::String play (double refHz, double sungHz, double lengthSec)
    {
        if (! opened)
        {
#if JUCE_WINDOWS
            manager.setCurrentAudioDeviceType ("Windows Audio", true);
#endif
            auto err = manager.initialiseWithDefaultDevices (0, 2);
            if (err.isNotEmpty()) return "No audio output could be opened: " + err;
            manager.addAudioCallback (this);
            opened = true;
        }
        if (manager.getCurrentAudioDevice() == nullptr)
            return "No audio output device is available.";
        const std::lock_guard<std::mutex> l (lock);
        seq.start (refHz, sungHz, lengthSec, sampleRate);
        return {};
    }

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { sampleRate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        if (n > (int) mono.size()) mono.assign ((size_t) n, 0.0f);
        {
            const std::lock_guard<std::mutex> l (lock);
            seq.render (mono.data(), n);
        }
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) std::copy (mono.begin(), mono.begin() + n, out[c]);
    }

    juce::AudioDeviceManager manager;
    std::mutex lock;
    ToneSequence seq;
    std::vector<float> mono = std::vector<float> (4096, 0.0f);
    double sampleRate = 48000.0;
    bool opened = false;
};
} // namespace va
