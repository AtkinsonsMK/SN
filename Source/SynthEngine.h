#pragma once

#include "SynthVoice.h"
#include <vector>

// The voice manager. Adds two things to JUCE's Synthesiser:
//  - a Voices limit (how many notes may sound at once);
//  - Mono mode (Voices = 1): one voice, last-note priority, legato. Playing a
//    new key while another is held slides/jumps the pitch without restarting
//    the envelopes; releasing it returns to the key still held.
class SynthEngine : public juce::Synthesiser
{
public:
    static constexpr int maxVoices = 16;

    explicit SynthEngine (VoiceSettings& s) : settings (s)
    {
        for (int i = 0; i < maxVoices; ++i)
            addVoice (new SynthVoice (settings, i));
        addSound (new SynthSound());
        held.reserve (128);
    }

    // Called once per block from the audio thread, before rendering.
    void setVoiceLimit (int n)
    {
        n = juce::jlimit (1, maxVoices, n);
        if (n == voiceLimit)
            return;

        const juce::ScopedLock sl (lock);
        if ((n == 1) != (voiceLimit == 1))
            held.clear();
        voiceLimit = n;
        settings.mono = (n == 1);
    }

    int getVoiceLimit() const { return voiceLimit; }

    int countActiveVoices() const
    {
        int n = 0;
        for (auto* v : voices)
            if (v->isVoiceActive())
                ++n;
        return n;
    }

    void noteOn (int channel, int note, float velocity) override
    {
        if (voiceLimit > 1)
        {
            juce::Synthesiser::noteOn (channel, note, velocity);
            return;
        }

        const juce::ScopedLock sl (lock);
        removeHeld (note);
        held.push_back ({ note, velocity });

        auto* voice = voices[0];
        const bool legato = voice->isVoiceActive() && voice->isKeyDown();
        startMono (voice, channel, note, velocity, legato);
    }

    void noteOff (int channel, int note, float velocity, bool allowTailOff) override
    {
        if (voiceLimit > 1)
        {
            juce::Synthesiser::noteOff (channel, note, velocity, allowTailOff);
            return;
        }

        const juce::ScopedLock sl (lock);
        removeHeld (note);

        auto* voice = voices[0];
        if (voice->getCurrentlyPlayingNote() == note && ! held.empty() && voice->isVoiceActive())
        {
            // Go back to the most recent key still held.
            const auto back = held.back();
            startMono (voice, channel, back.note, back.velocity, true);
            return;
        }

        // Releases voice 0 if it's playing this note, and any older poly
        // voices left over from before switching to Mono.
        juce::Synthesiser::noteOff (channel, note, velocity, allowTailOff);
    }

    void allNotesOff (int channel, bool allowTailOff) override
    {
        const juce::ScopedLock sl (lock);
        held.clear();
        juce::Synthesiser::allNotesOff (channel, allowTailOff);
    }

protected:
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* sound, int channel,
                                           int note, bool steal) const override
    {
        const juce::ScopedLock sl (lock);

        if (countActiveVoices() < voiceLimit)
            for (auto* v : voices)
                if (! v->isVoiceActive() && v->canPlaySound (sound))
                    return v;

        return steal ? findVoiceToSteal (sound, channel, note) : nullptr;
    }

    // At the limit: re-use the oldest released note first, then the oldest
    // held note (but never the lowest held note, which is usually the bass).
    juce::SynthesiserVoice* findVoiceToSteal (juce::SynthesiserSound*, int, int) const override
    {
        juce::SynthesiserVoice* oldestReleased = nullptr;
        juce::SynthesiserVoice* oldestHeld = nullptr;
        juce::SynthesiserVoice* lowestHeld = nullptr;

        for (auto* v : voices)
            if (v->isVoiceActive() && v->isKeyDown())
                if (lowestHeld == nullptr || v->getCurrentlyPlayingNote() < lowestHeld->getCurrentlyPlayingNote())
                    lowestHeld = v;

        for (auto* v : voices)
        {
            if (! v->isVoiceActive())
                continue;
            if (! v->isKeyDown())
            {
                if (oldestReleased == nullptr || v->wasStartedBefore (*oldestReleased))
                    oldestReleased = v;
            }
            else if (v != lowestHeld)
            {
                if (oldestHeld == nullptr || v->wasStartedBefore (*oldestHeld))
                    oldestHeld = v;
            }
        }

        if (oldestReleased != nullptr) return oldestReleased;
        if (oldestHeld != nullptr)     return oldestHeld;
        if (lowestHeld != nullptr)     return lowestHeld;
        return voices[0];
    }

private:
    struct HeldKey { int note; float velocity; };

    void startMono (juce::SynthesiserVoice* voice, int channel, int note, float velocity, bool legato)
    {
        settings.legatoNext = legato;
        startVoice (voice, getSound (0).get(), channel, note, velocity);
        settings.legatoNext = false;
    }

    void removeHeld (int note)
    {
        held.erase (std::remove_if (held.begin(), held.end(),
                                    [note] (const HeldKey& k) { return k.note == note; }),
                    held.end());
    }

    VoiceSettings& settings;
    int voiceLimit = 8;
    std::vector<HeldKey> held;
};
