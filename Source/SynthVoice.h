#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "Envelope.h"
#include "Filters.h"
#include "LiveInfo.h"
#include "Oscillator.h"
#include "Modulation.h"

// Values the processor hands to every voice once per audio block.
struct VoiceSettings
{
    OscSettings osc[2];
    float subLevel = 0.0f;                       // 0..1, one octave below Osc 1
    float fm1to2 = 0.0f, fm2to1 = 0.0f;          // 0..1
    float ringMod = 0.0f;                        // 0..1
    float fenvA = 0.001f, fenvD = 0.4f, fenvS = 0.3f, fenvR = 0.3f;
    float aenvA = 0.001f, aenvD = 0.3f, aenvS = 1.0f, aenvR = 0.2f;
    float lfoToCutoff = 0.0f; // 0..1
    float drift = 0.15f;      // 0..1
    float glideSeconds = 0.0f;

    FilterSlotSettings slots[3];
    int order[Chain::numBlocks] = { 0, 1, 2, 3, 4 }; // processing order: filters 0-2, 3 Amp, 4 EQ
    float eqLowDb = 0.0f, eqMidDb = 0.0f, eqHighDb = 0.0f;

    // For the displays: per-sample stage outputs summed over all voices
    // (0 oscillator, 1-3 after each filter position), and live note info.
    float* taps[1 + Chain::numBlocks] = {}; // 0 oscillators, then after each chain position
    LiveInfo* live = nullptr;
    int noteCounter = 0;   // increases with every note played
    int newestNote = 0;    // the counter value of the most recent note

    const float* lfo[2] = { nullptr, nullptr }; // LFO 1 and 2, one value (-1..1) per sample of the block
    Mod::Context* mod = nullptr;                // routings and knob positions for modulation
    float velocityAmount = 0.5f;  // 0..1: how much velocity changes the volume
    float keyTracking = 0.5f;     // 0..1: how much the filter cutoff follows the note
    float bendRange = 2.0f;       // semitones
    double lastNote = -1.0;     // most recent note played, for Glide
    bool mono = false;          // Voices = 1
    bool legatoNext = false;    // set by the engine: next startNote is a legato move
};

// Writes one knob's value (in its own units) into the settings. Used both
// for the plain knob values and for modulated ones, so they always agree.
inline void applyTarget (VoiceSettings& v, int t, float x)
{
    using namespace Mod;
    if (t >= f1Cutoff && t <= f3Char)
    {
        auto& slot = v.slots[(t - f1Cutoff) / 5];
        switch ((t - f1Cutoff) % 5)
        {
            case 0: slot.cutoffHz  = x; break;
            case 1: slot.resonance = x * 0.01f; break;
            case 2: slot.drive     = x * 0.01f; break;
            case 3: slot.envAmount = x * 0.01f; break;
            default: slot.character = x * 0.01f; break;
        }
        return;
    }
    auto& o1 = v.osc[0];
    auto& o2 = v.osc[1];
    switch (t)
    {
        case osc1Sine:   o1.sine = x * 0.01f; break;
        case osc1Saw:    o1.saw = x * 0.01f; break;
        case osc1Pulse:  o1.pulse = x * 0.01f; break;
        case osc1Pw:     o1.pulseWidth = x * 0.01f; break;
        case osc1Coarse: o1.coarse = x; break;
        case osc1Fine:   o1.fine = x; break;
        case osc1Level:  o1.level = x * 0.01f; break;
        case osc2Sine:   o2.sine = x * 0.01f; break;
        case osc2Saw:    o2.saw = x * 0.01f; break;
        case osc2Pulse:  o2.pulse = x * 0.01f; break;
        case osc2Pw:     o2.pulseWidth = x * 0.01f; break;
        case osc2Coarse: o2.coarse = x; break;
        case osc2Fine:   o2.fine = x; break;
        case osc2Level:  o2.level = x * 0.01f; break;
        case sub:        v.subLevel = x * 0.01f; break;
        case fm12:       v.fm1to2 = x * 0.01f; break;
        case fm21:       v.fm2to1 = x * 0.01f; break;
        case ring:       v.ringMod = x * 0.01f; break;
        case fenvA:      v.fenvA = x; break;
        case fenvD:      v.fenvD = x; break;
        case fenvS:      v.fenvS = x * 0.01f; break;
        case fenvR:      v.fenvR = x; break;
        case aenvA:      v.aenvA = x; break;
        case aenvD:      v.aenvD = x; break;
        case aenvS:      v.aenvS = x * 0.01f; break;
        case aenvR:      v.aenvR = x; break;
        case Mod::drift: v.drift = x * 0.01f; break;
        case Mod::glide: v.glideSeconds = x; break;
        case Mod::eqLow:  v.eqLowDb = x; break;
        case Mod::eqMid:  v.eqMidDb = x; break;
        case Mod::eqHigh: v.eqHighDb = x; break;
        default: break; // Volume is handled by the processor
    }
}

// One EQ band for one voice (transposed direct form II).
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (const std::array<float, 6>& c)
    {
        const float inv = 1.0f / c[3];
        b0 = c[0] * inv; b1 = c[1] * inv; b2 = c[2] * inv; a1 = c[4] * inv; a2 = c[5] * inv;
    }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }
};

// The 3-band EQ (Low shelf 100 Hz, Mid bell 1 kHz, High shelf 8 kHz).
struct VoiceEq
{
    Biquad low, mid, high;
    float lastDb[3] = { 1000.0f, 1000.0f, 1000.0f };
    bool flat = true;

    void update (double sr, float lowDb, float midDb, float highDb)
    {
        using C = juce::dsp::IIR::ArrayCoefficients<float>;
        if (std::abs (lowDb - lastDb[0]) > 0.01f)  { low.set  (C::makeLowShelf  (sr, 100.0,  0.707f, juce::Decibels::decibelsToGain (lowDb)));  lastDb[0] = lowDb; }
        if (std::abs (midDb - lastDb[1]) > 0.01f)  { mid.set  (C::makePeakFilter (sr, 1000.0, 0.7f,   juce::Decibels::decibelsToGain (midDb)));  lastDb[1] = midDb; }
        if (std::abs (highDb - lastDb[2]) > 0.01f) { high.set (C::makeHighShelf (sr, 8000.0, 0.707f, juce::Decibels::decibelsToGain (highDb))); lastDb[2] = highDb; }
        flat = std::abs (lowDb) < 0.01f && std::abs (midDb) < 0.01f && std::abs (highDb) < 0.01f;
    }
    float process (float x) { return high.process (mid.process (low.process (x))); }
    void reset() { low.reset(); mid.reset(); high.reset(); }
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr float  maxDriftCents           = 10.0f;
    static constexpr int    modInterval             = 16; // modulation is updated every 16 samples

    static constexpr float envOctavesAtFull   = 6.0f;  // Env Amount 100% sweeps 6 octaves
    static constexpr float lfoOctavesAtFull   = 3.0f;  // LFO to Cutoff 100% = +/- 3 octaves
    static constexpr double fmDepthAtFull     = 0.5;   // FM 100% pushes the phase up to half a cycle

    SynthVoice (VoiceSettings& s, int voiceIndex) : shared (s), cur (s), random ((juce::int64) (voiceIndex * 7919 + 17)) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate <= 0.0)
            return;

        ampEnv.setSampleRate (newRate);
        filterEnv.setSampleRate (newRate);

        for (auto& f : filters)
            f.prepare ((float) newRate);

        smoothCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) newRate)); // 5 ms
        driftCoef  = 1.0f - std::exp (-1.0f / (0.4f  * (float) newRate));  // 0.4 s
        driftValue = (float) (random.nextFloat() * 2.0 - 1.0);
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override
    {
        bendSemitones = wheelToSemitones (pitchWheel);

        if (shared.legatoNext && ampEnv.isActive())
        {
            legatoTo (midiNote);
            return;
        }

        targetNote = midiNote;
        const double sr = getSampleRate();
        // Poly: every note glides from the last one played. Mono: only
        // overlapping (legato) notes glide.
        if (! shared.mono && shared.glideSeconds > 0.0005f && shared.lastNote >= 0.0)
            glideFrom (shared.lastNote);
        else
        {
            currentNote = targetNote;
            gliding = false;
        }
        shared.lastNote = targetNote;
        noteSerial = ++shared.noteCounter;
        shared.newestNote = noteSerial;

        // Velocity to Volume: at 50% the softest note is half as loud as the hardest.
        noteVelocity = velocity;
        noteGain = (1.0f - shared.velocityAmount) + shared.velocityAmount * velocity;

        if (! ampEnv.isActive())
        {
            osc1.reset (shared.osc[0]);
            osc2.reset (shared.osc[1]);
            subPhase = 0.0;
            for (auto& f : filters)
                f.reset();
            eq.reset();
            sSub = shared.subLevel;
            sFm12 = shared.fm1to2; sFm21 = shared.fm2to1; sRing = shared.ringMod;
            sLvl1 = shared.osc[0].level; sLvl2 = shared.osc[1].level;
        }

        for (auto& f : filters)
            f.trigger ((float) sr);

        refreshSettings (0);
        ampEnv.noteOn();
        filterEnv.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            filterEnv.noteOff();
        }
        else
        {
            // Stopped to make room for another note (voice stealing): keep
            // the envelopes where they are, so the new note carries on from
            // the current level instead of clicking.
            clearCurrentNote();
        }
    }

private:
    // Mono, legato: move to a new note without restarting the envelopes.
    // Glide slides there if it's turned up.
    void legatoTo (int midiNote)
    {
        const double from = currentNote;
        targetNote = midiNote;
        if (shared.glideSeconds > 0.0005f)
            glideFrom (from);
        else
        {
            currentNote = targetNote;
            gliding = false;
        }
        shared.lastNote = targetNote;
        noteSerial = ++shared.noteCounter;
        shared.newestNote = noteSerial;
    }

public:
    void pitchWheelMoved (int newValue) override { bendSemitones = wheelToSemitones (newValue); }
    void controllerMoved (int, int) override {}

    using juce::SynthesiserVoice::renderNextBlock;

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const double sr = getSampleRate();
        const int numChannels = output.getNumChannels();
        float baseOct[3] {};
        double tune1 = 1.0, tune2 = 1.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const int pos = startSample + i;

            // Every 16 samples: take this voice's copy of the knobs and apply
            // its modulation (LFOs, envelopes, velocity, mod wheel, key).
            if (i % modInterval == 0)
            {
                refreshSettings (pos);
                const float keyTrackOct = cur.keyTracking * (float) (currentNote - 60.0) / 12.0f; // follows Glide
                for (int s = 0; s < 3; ++s)
                    baseOct[s] = std::log2 (cur.slots[s].cutoffHz) + keyTrackOct;
                eq.update (sr, cur.eqLowDb, cur.eqMidDb, cur.eqHighDb);
                tune1 = std::exp2 (cur.osc[0].tuneSemitones() / 12.0);
                tune2 = std::exp2 (cur.osc[1].tuneSemitones() / 12.0);
            }

            // Parameter smoothing (avoids zipper noise when knobs move).
            sSub   += (cur.subLevel - sSub)   * smoothCoef;
            sFm12  += (cur.fm1to2   - sFm12)  * smoothCoef;
            sFm21  += (cur.fm2to1   - sFm21)  * smoothCoef;
            sRing  += (cur.ringMod  - sRing)  * smoothCoef;
            sLvl1  += (cur.osc[0].level - sLvl1) * smoothCoef;
            sLvl2  += (cur.osc[1].level - sLvl2) * smoothCoef;

            // Glide
            if (gliding)
            {
                currentNote += glideStep;
                if ((glideStep > 0.0 && currentNote >= targetNote) || (glideStep < 0.0 && currentNote <= targetNote))
                {
                    currentNote = targetNote;
                    gliding = false;
                }
            }

            // Drift: slow random wander per voice.
            if (--driftCounter <= 0)
            {
                driftTarget = random.nextFloat() * 2.0f - 1.0f;
                driftCounter = (int) (sr * (0.3 + 0.7 * random.nextFloat()));
            }
            driftValue += (driftTarget - driftValue) * driftCoef;
            const double driftSemis = cur.drift * maxDriftCents * driftValue / 100.0;

            const double pitch = currentNote + bendSemitones * cur.bendRange + driftSemis;
            const double freq = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
            const double inc1 = juce::jmin (freq * tune1 / sr, 0.45);
            const double inc2 = juce::jmin (freq * tune2 / sr, 0.45);

            // Each oscillator can push the other's phase (FM). Osc 2 -> 1
            // uses Osc 2's previous sample, which is how a feedback loop works.
            const float o1 = osc1.next (inc1, sFm21 * fmDepthAtFull * osc2.getLastOut(), cur.osc[0], smoothCoef);
            const float o2 = osc2.next (inc2, sFm12 * fmDepthAtFull * o1, cur.osc[1], smoothCoef);
            const float subOut = sSub > 1.0e-4f ? sub (inc1 * 0.5) : 0.0f;
            subPhase += inc1 * 0.5;
            if (subPhase >= 1.0) subPhase -= 1.0;

            const float osc = 0.3f * (sLvl1 * o1 + sSub * subOut + sLvl2 * o2 + sRing * o1 * o2);

            // Filter rack: slots in series, in the chosen order.
            const float fenv = filterEnv.next();
            const float lfo = cur.lfo[0] != nullptr ? cur.lfo[0][pos] : 0.0f;
            const float lfoOct = lfo * cur.lfoToCutoff * lfoOctavesAtFull; // older projects' "LFO to Cutoff"

            const float gain = ampEnv.next() * noteGain;
            if (cur.taps[0] != nullptr) cur.taps[0][pos] += osc * gain;

            // The chain: Filters 1-3, Amp and EQ in the chosen order.
            float x = osc;
            bool ampDone = false;
            for (int n = 0; n < Chain::numBlocks; ++n)
            {
                const int b = cur.order[n];
                if (Chain::isFilter (b))
                {
                    const auto& slot = cur.slots[b];
                    const float cutoffOct = baseOct[b] + fenv * slot.envAmount * envOctavesAtFull + lfoOct;
                    lastCutoffHz[b] = std::exp2 (juce::jlimit (4.3f, 14.3f, cutoffOct));
                    x = filters[b].process (x, slot, lastCutoffHz[b]);
                }
                else if (b == Chain::amp)
                {
                    x *= gain;
                    ampDone = true;
                }
                else if (! eq.flat)
                {
                    x = eq.process (x);
                }
                // The displays show each stage at the level you'd hear it.
                if (cur.taps[n + 1] != nullptr) cur.taps[n + 1][pos] += ampDone ? x : x * gain;
            }
            lastFreq = (float) (freq * tune1);

            const float sample = x;

            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, pos, sample);

            if (! ampEnv.isActive())
            {
                filterEnv.reset();
                clearCurrentNote();
                break;
            }
        }

        publishLiveInfo();
    }

private:
    // The newest note tells the screen what to draw.
    void publishLiveInfo()
    {
        auto* live = shared.live;
        if (live == nullptr || noteSerial != shared.newestNote)
            return;

        live->noteActive.store (isVoiceActive());
        live->noteHz.store (lastFreq);
        for (int s = 0; s < 3; ++s)
        {
            live->liveCutoffHz[s].store (lastCutoffHz[s]);
            live->gateLevel[s].store (shared.slots[s].type == FilterType::lowpassGate ? filters[s].gate() : 1.0f);
        }
        auto publish = [] (LiveInfo::EnvState& out, const Envelope& e, float a, float d, float r)
        {
            out.attack.store (a);
            out.decay.store (d);
            out.release.store (r);
            out.sustain.store (e.sustainLevel());
            out.startLevel.store (e.stageStartLevel());
            out.level.store (e.level());
            out.stage.store (e.stageIndex());
        };
        publish (live->filterEnv, filterEnv, cur.fenvA, cur.fenvD, cur.fenvR);
        publish (live->ampEnv, ampEnv, cur.aenvA, cur.aenvD, cur.aenvR);
    }

    void applyEnvelopeSettings()
    {
        ampEnv.setParameters    (cur.aenvA, cur.aenvD, cur.aenvS, cur.aenvR);
        filterEnv.setParameters (cur.fenvA, cur.fenvD, cur.fenvS, cur.fenvR);
    }

    // This voice's source values right now.
    void currentSources (int pos, float* src) const
    {
        src[Mod::lfo1]      = shared.lfo[0] != nullptr ? shared.lfo[0][pos] : 0.0f;
        src[Mod::lfo2]      = shared.lfo[1] != nullptr ? shared.lfo[1][pos] : 0.0f;
        src[Mod::filterEnv] = filterEnv.level();
        src[Mod::ampEnv]    = ampEnv.level();
        src[Mod::velocity]  = noteVelocity;
        src[Mod::modWheel]  = shared.mod != nullptr ? shared.mod->modWheel : 0.0f;
        src[Mod::key]       = juce::jlimit (-1.0f, 1.0f, (float) (targetNote - 60) / 60.0f);
    }

    void refreshSettings (int pos)
    {
        cur = shared;
        auto* m = shared.mod;
        if (m != nullptr)
        {
            float src[Mod::numSources];
            currentSources (pos, src);

            if (noteSerial == shared.newestNote)
                for (int s = 0; s < Mod::numSources; ++s)
                    m->newestSources[(size_t) s] = src[s];

            if (m->anyVoiceRoutes)
            {
                float offset[Mod::numTargets] {};
                bool touched[Mod::numTargets] {};
                for (int r = 0; r < m->numRoutes; ++r)
                {
                    const auto& route = m->routes[(size_t) r];
                    if (Mod::isGlobal (route.target))
                        continue;
                    offset[route.target] += src[route.source] * route.amount;
                    touched[route.target] = true;
                }
                for (int t = 0; t < Mod::numTargets; ++t)
                    if (touched[t])
                        applyTarget (cur, t, Mod::applyOffset (*m, t, offset[t]));
            }
        }
        applyEnvelopeSettings();
    }

    // Returns -1..1; scaled by Bend Range when used.
    static double wheelToSemitones (int wheel)
    {
        return (wheel - 8192) / 8192.0;
    }

    float sub (double inc) const
    {
        double v = subPhase < 0.5 ? 1.0 : -1.0;
        v += Oscillator::polyBlep (subPhase, inc);
        double t = subPhase + 0.5;
        if (t >= 1.0) t -= 1.0;
        v -= Oscillator::polyBlep (t, inc);
        return (float) v;
    }

    void glideFrom (double fromNote)
    {
        currentNote = fromNote;
        glideStep = (targetNote - currentNote) / (shared.glideSeconds * getSampleRate());
        gliding = std::abs (targetNote - currentNote) > 1.0e-9;
    }

    VoiceSettings& shared;  // written by the processor once per block
    VoiceSettings cur;      // this voice's copy, with its modulation applied
    float noteVelocity = 1.0f;
    juce::Random random;

    Envelope ampEnv, filterEnv;
    FilterSlot filters[3];
    VoiceEq eq;

    int    targetNote    = 60;
    double currentNote   = 60.0;
    double glideStep     = 0.0;
    bool   gliding       = false;
    double bendSemitones = 0.0;
    Oscillator osc1, osc2;
    double subPhase = 0.0;
    float  noteGain = 1.0f;
    int    noteSerial = 0;
    float  lastFreq = 0.0f;
    float  lastCutoffHz[3] = { 1000.0f, 1000.0f, 1000.0f };

    float smoothCoef = 0.01f;
    float sSub = 0.0f, sFm12 = 0.0f, sFm21 = 0.0f, sRing = 0.0f, sLvl1 = 1.0f, sLvl2 = 0.0f;

    float driftCoef = 0.0001f, driftValue = 0.0f, driftTarget = 0.0f;
    int   driftCounter = 0;
};
