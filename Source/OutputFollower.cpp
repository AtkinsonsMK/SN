#include "OutputFollower.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>

// Listens for macOS changing its default output device. CoreAudio calls
// back on its own thread, so this only flags the change for the message thread.
struct OutputFollower::Listener
{
    explicit Listener (juce::AsyncUpdater& u) : updater (u)
    {
        AudioObjectAddPropertyListener (kAudioObjectSystemObject, &address, callback, this);
    }

    ~Listener()
    {
        AudioObjectRemovePropertyListener (kAudioObjectSystemObject, &address, callback, this);
    }

    static OSStatus callback (AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* data)
    {
        static_cast<Listener*> (data)->updater.triggerAsyncUpdate();
        return noErr;
    }

    juce::AsyncUpdater& updater;
    AudioObjectPropertyAddress address { kAudioHardwarePropertyDefaultOutputDevice,
                                         kAudioObjectPropertyScopeGlobal,
                                         0 }; // main element
};
#else
struct OutputFollower::Listener
{
    explicit Listener (juce::AsyncUpdater&) {}
};
#endif

OutputFollower::OutputFollower()
{
    listener = std::make_unique<Listener> (static_cast<juce::AsyncUpdater&> (*this));
    // The app opens the device it used last time; once that's done, move to
    // the current system output (for example headphones plugged in while
    // the app was closed).
    startTimer (300);
}

OutputFollower::~OutputFollower()
{
    listener.reset();
    cancelPendingUpdate();
    stopTimer();
}

void OutputFollower::handleAsyncUpdate()
{
    // Give macOS a moment to finish listing the new device before switching.
    attempts = 0;
    startTimer (300);
}

void OutputFollower::timerCallback()
{
    if (followNow() || ++attempts > 20)
    {
        stopTimer();
        attempts = 0;
    }
}

bool OutputFollower::followNow()
{
   #if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return false;

    auto& dm = holder->deviceManager;
    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr || dm.getCurrentAudioDevice() == nullptr)
        return false;

    type->scanForDevices();
    const auto names = type->getDeviceNames (false);
    const int index = type->getDefaultDeviceIndex (false);
    if (! juce::isPositiveAndBelow (index, names.size()))
        return false;

    const auto systemOutput = names[index];
    auto setup = dm.getAudioDeviceSetup();
    if (setup.outputDeviceName == systemOutput)
        return true;

    setup.outputDeviceName = systemOutput;
    const auto error = dm.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        return false;

    holder->saveAudioDeviceState();
    return true;
   #else
    return true;
   #endif
}
