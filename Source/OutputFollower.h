#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

// Standalone app only: keeps the audio output on the Mac's current system
// output. When headphones are plugged in (or the output is changed in
// System Settings or the menu bar), the app moves to that device too.
// Choosing a device in Options still works; it holds until the system
// output next changes. Does nothing in a DAW.
class OutputFollower : private juce::Timer,
                       private juce::AsyncUpdater
{
public:
    OutputFollower();
    ~OutputFollower() override;

    // Moves to the system output now, if it isn't on it already. Returns
    // false if the standalone app's audio isn't set up yet.
    bool followNow();

private:
    void timerCallback() override;
    void handleAsyncUpdate() override;

    struct Listener;
    std::unique_ptr<Listener> listener;
    int attempts = 0;
};
