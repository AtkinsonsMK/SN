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
    float fenvCurve = 0.0f, aenvCurve = 0.0f; // -1..1, see Envelope::setParameters
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
    float spread = 0.0f;          // 0..1: stereo width of the voices
    float variation = 0.0f;       // 0..1: fixed differences between voices
    bool  randomPhase = false;    // each new note starts its waves at a random point
    int   othersSounding = 0;     // set by the engine: notes already sounding when this one starts
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
    static inline double    osBlepWidth             = 3.0; // jump smoothing width at 4x (samples); the normal path's is 4
    static constexpr int    modInterval             = 16; // modulation is updated every 16 samples

    static constexpr float envOctavesAtFull   = 6.0f;  // Env Amount 100% sweeps 6 octaves
    static constexpr float lfoOctavesAtFull   = 3.0f;  // LFO to Cutoff 100% = +/- 3 octaves
    static constexpr double fmDepthAtFull     = 0.5;   // FM 100% pushes the phase up to half a cycle

    // Voice Variation at 100%: how far each voice's fixed differences can go.
    static constexpr float varTuneCents  = 8.0f;   // each oscillator's tuning, +/- cents
    static constexpr float varCutoffOct  = 0.3f;   // filter cutoffs, +/- octaves (about +/-23%)
    static constexpr float varEnvOct     = 0.32f;  // envelope times, x/÷ 2^0.32 (about +/-25%)
    static constexpr float varLevelDb    = 1.0f;   // loudness, +/- dB
    static constexpr float bassMonoHz    = 150.0f; // below this, every voice stays in the centre

    SynthVoice (VoiceSettings& s, int voiceIndex) : shared (s), cur (s), random ((juce::int64) (voiceIndex * 7919 + 17))
    {
        // Each voice's own fixed "component tolerances", like the voice cards
        // of a polyphonic analogue synth. Voice Variation scales them.
        juce::Random tolRandom ((juce::int64) (voiceIndex * 104729 + 3));
        for (auto& t : tol)
            t = tolRandom.nextFloat() * 2.0f - 1.0f;
    }

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
        for (auto& f : fadeFilters)
            f.prepare ((float) newRate);
        typeFadeLen = juce::jmax (1, (int) (0.01 * newRate)); // 10 ms

        smoothCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) newRate)); // 5 ms
        smoothCoef4x = 1.0f - std::exp (-1.0f / (0.005f * 4.0f * (float) newRate));
        fmLpCoef = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 16000.0f / (4.0f * (float) newRate));
        // FM / ring mod oversampling: keep up to 20 kHz (or just under the top at low rates).
        const double keep = std::min (20000.0, newRate * 0.4535);
        oscDown1.design (keep / (2.0 * newRate));
        oscDown2.design (keep / (4.0 * newRate));
        blockSmoothCoef = 1.0f - std::exp (-(float) modInterval / (0.005f * (float) newRate));
        driftCoef  = 1.0f - std::exp (-1.0f / (0.4f  * (float) newRate));  // 0.4 s
        for (auto& d : drift)
            d.value = d.target = random.nextFloat() * 2.0f - 1.0f;
        panCoef   = 1.0f - std::exp (-1.0f / (0.01f * (float) newRate));    // 10 ms
        bassCoef  = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * bassMonoHz / (float) newRate);
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
        if (shared.variation > 0.0f)
            noteGain *= juce::Decibels::decibelsToGain (shared.variation * tol[tolLevel] * varLevelDb);

        // Stereo place: a note on its own sits in the middle; each extra note
        // sounding with it takes the next place out (left, right, further...),
        // so a chord fans out while a single melody line stays centred.
        // The same note played again in this voice keeps its place.
        const bool sameNoteGoesOn = ampEnv.isActive() && midiNote == panNote;
        if (! sameNoteGoesOn)
        {
            static constexpr float pattern[8] = { 0.0f, -1.0f, 1.0f, -0.6f, 0.6f, -0.8f, 0.8f, -0.4f };
            // Low notes stay nearer the centre: C2 and below in the middle,
            // full width from C3 up.
            const float lowKeep = juce::jlimit (0.0f, 1.0f, (float) (midiNote - 36) / 12.0f);
            panPos = shared.mono ? 0.0f : pattern[(size_t) (shared.othersSounding & 7)] * lowKeep;
        }
        panNote = midiNote;

        if (! ampEnv.isActive())
        {
            noteGainS = noteGain; // a fresh note starts at its own level
            oscOversample = false;
            oscDown1.reset(); oscDown2.reset();
            fmLp12[0] = fmLp12[1] = fmLp21[0] = fmLp21[1] = 0.0f;
            bendS = bendSemitones;
            dcX1 = dcY1 = 0.0f;
            osc1.reset (shared.osc[0]);
            osc2.reset (shared.osc[1]);
            subPhase = 0.0;
            if (shared.randomPhase)
            {
                // Start the waves at a random point, so no two notes begin
                // identically. Both oscillators (and the sub, an octave down)
                // start together, so their blend, and so the loudness, stays
                // the same from note to note.
                const double u = random.nextDouble();
                subPhase = u;
                osc1.setPhase (2.0 * u);
                osc2.setPhase (2.0 * u);
            }
            for (auto& f : filters)
                f.reset();
            for (int b = 0; b < 3; ++b) { lastType[b] = -1; typeFadeLeft[b] = 0; } // a new note takes the types as they are
            eq.reset();
            sSub = shared.subLevel;
            sFm12 = shared.fm1to2; sFm21 = shared.fm2to1; sRing = shared.ringMod;
            sLvl1 = shared.osc[0].level; sLvl2 = shared.osc[1].level;
            bassS = 0.0f;
            snapPan = true;
            snapSmoothing = true; // filter and tuning smoothing start at the knob values
        }

        for (auto& f : filters)
            f.trigger ((float) sr);

        refreshSettings (0);
        needsRefresh = true; // again at the note's exact sample, when it starts rendering
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
        const float gainGlideCoef = 1.0f - (float) std::exp (-1.0 / (0.002 * sr));
        const double bendGlideCoef = 1.0 - std::exp (-1.0 / (0.003 * sr));
        const float dcCoef = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 5.0 / sr);

        for (int i = 0; i < numSamples; ++i)
        {
            const int pos = startSample + i;

            // Every 16 samples: take this voice's copy of the knobs and apply
            // its modulation (LFOs, envelopes, velocity, mod wheel, key).
            if (needsRefresh || (shared.mod != nullptr ? shared.mod->onGrid (pos) : (pos % modInterval == 0)))
            {
                needsRefresh = false;
                refreshSettings (pos);
                const float keyTrackOct = cur.keyTracking * (float) (currentNote - 60.0) / 12.0f // follows Glide
                                          + cur.variation * tol[tolCutoff] * varCutoffOct;
                // The knobs (and DAW automation) are smoothed; the modulation
                // is added afterwards, gliding in a straight line to each new
                // 16-sample value, so fast LFOs and envelopes keep their full
                // depth and snap.
                for (int s = 0; s < 3; ++s)
                {
                    const auto& k = knobSlots[s];
                    const auto& c = cur.slots[s];
                    baseOct[s] = std::log2 (k.cutoffHz) + keyTrackOct;
                    const float target[numSlotMods] { std::log2 (c.cutoffHz) - std::log2 (k.cutoffHz), c.resonance - k.resonance,
                                                      c.drive - k.drive, c.envAmount - k.envAmount, c.character - k.character };
                    for (int j = 0; j < numSlotMods; ++j)
                    {
                        if (snapSmoothing) modNow[s][j] = target[j];
                        modStep[s][j] = (target[j] - modNow[s][j]) / (float) modInterval;
                    }
                }
                modStepsLeft = modInterval;
                if (snapSmoothing)
                {
                    for (int s = 0; s < 3; ++s)
                    {
                        const auto& slot = knobSlots[s];
                        sOct[s] = baseOct[s]; sRes[s] = slot.resonance; sDrive[s] = slot.drive;
                        sEnvAmt[s] = slot.envAmount; sChar[s] = slot.character;
                    }
                    sTune1 = tuneTarget (0); sTune2 = tuneTarget (1);
                    sEq[0] = cur.eqLowDb; sEq[1] = cur.eqMidDb; sEq[2] = cur.eqHighDb;
                    snapSmoothing = false;
                }
                // EQ gains glide too (its coefficients are worked out every 16 samples).
                sEq[0] += (cur.eqLowDb  - sEq[0]) * blockSmoothCoef;
                sEq[1] += (cur.eqMidDb  - sEq[1]) * blockSmoothCoef;
                sEq[2] += (cur.eqHighDb - sEq[2]) * blockSmoothCoef;
                eq.update (sr, sEq[0], sEq[1], sEq[2]);

                // Stereo place (equal loudness at every position).
                const float place = panPos * std::sqrt (cur.spread); // a gentle taper: 50% already sounds wide
                const bool centred = std::abs (place) < 1.0e-6f;
                const float theta = (place + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                panL = centred ? 1.0f : juce::MathConstants<float>::sqrt2 * std::cos (theta);
                panR = centred ? 1.0f : juce::MathConstants<float>::sqrt2 * std::sin (theta);
                if (snapPan) { panLS = panL; panRS = panR; snapPan = false; }
                panMoving = std::abs (panLS - panL) + std::abs (panRS - panR) > 1.0e-6f;
                panActive = panMoving || ! centred;
            }

            // Smooth every continuous filter and tuning setting sample by
            // sample (about 5 ms), so knob moves, automation and modulation
            // glide instead of stepping ("zipper" noise).
            const bool modMoving = modStepsLeft > 0;
            if (modMoving) --modStepsLeft;
            for (int s = 0; s < 3; ++s)
            {
                const auto& slot = knobSlots[s];
                if (modMoving)
                    for (int j = 0; j < numSlotMods; ++j)
                        modNow[s][j] += modStep[s][j];
                sOct[s]    += (baseOct[s]     - sOct[s])    * smoothCoef;
                sRes[s]    += (slot.resonance - sRes[s])    * smoothCoef;
                sDrive[s]  += (slot.drive     - sDrive[s])  * smoothCoef;
                sEnvAmt[s] += (slot.envAmount - sEnvAmt[s]) * smoothCoef;
                sChar[s]   += (slot.character - sChar[s])   * smoothCoef;
            }
            sTune1 += (tuneTarget (0) - sTune1) * smoothCoef;
            sTune2 += (tuneTarget (1) - sTune2) * smoothCoef;

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

            // Drift: slow random wander, separate for each oscillator of each
            // voice, as two real analogue oscillators never drift together.
            for (auto& d : drift)
            {
                if (--d.counter <= 0)
                {
                    d.target = random.nextFloat() * 2.0f - 1.0f;
                    d.counter = (int) (sr * (0.3 + 0.7 * random.nextFloat()));
                }
                d.value += (d.target - d.value) * driftCoef;
            }
            const double driftScale = cur.drift * maxDriftCents / 100.0;

            // Pitch bend glides over about 3 ms, so the wheel's steps can't be heard.
            bendS += (bendSemitones - bendS) * bendGlideCoef;
            const double pitch = currentNote + bendS * cur.bendRange;
            const double freq1 = 440.0 * std::exp2 ((pitch + driftScale * drift[0].value + sTune1 - 69.0) / 12.0);
            const double freq2 = 440.0 * std::exp2 ((pitch + driftScale * drift[1].value + sTune2 - 69.0) / 12.0);
            const double inc1 = juce::jmin (freq1 / sr, 0.45);
            const double inc2 = juce::jmin (freq2 / sr, 0.45);

            // Each oscillator can push the other's phase (FM). Osc 2 -> 1
            // uses Osc 2's previous sample, which is how a feedback loop works.
            // FM and ring mod create tones far above the top of hearing, which
            // fold back down as harsh, out-of-tune grit. While either is in
            // use, the two oscillators run at 4x the rate and are filtered
            // back down, so those tones are removed instead of folding back.
            if (! oscOversample && (sFm12 > 1.0e-4f || sFm21 > 1.0e-4f || sRing > 1.0e-4f
                                    || cur.fm1to2 > 1.0e-4f || cur.fm2to1 > 1.0e-4f || cur.ringMod > 1.0e-4f))
                oscOversample = true; // stays on until the note ends
            float pair;
            if (oscOversample)
            {
                float m[4];
                for (int k = 0; k < 4; ++k)
                {
                    // The FM signals are softened above about 16 kHz first: a saw's
                    // sudden jump would otherwise jerk the other oscillator's phase
                    // and spray tones everywhere.
                    fmLp21[0] += (osc2.getLastOut() - fmLp21[0]) * fmLpCoef; fmLp21[1] += (fmLp21[0] - fmLp21[1]) * fmLpCoef;
                    const float a = osc1.next (inc1 * 0.25, sFm21 * fmDepthAtFull * fmLp21[1], cur.osc[0], smoothCoef4x, osBlepWidth);
                    fmLp12[0] += (a - fmLp12[0]) * fmLpCoef; fmLp12[1] += (fmLp12[0] - fmLp12[1]) * fmLpCoef;
                    const float b = osc2.next (inc2 * 0.25, sFm12 * fmDepthAtFull * fmLp12[1], cur.osc[1], smoothCoef4x, osBlepWidth);
                    m[k] = sLvl1 * a + sLvl2 * b + sRing * a * b;
                }
                const float h0 = oscDown2.down (m[0], m[1]); // (in time order)
                const float h1 = oscDown2.down (m[2], m[3]);
                pair = oscDown1.down (h0, h1);
            }
            else
            {
                const float o1 = osc1.next (inc1, 0.0, cur.osc[0], smoothCoef);
                const float o2 = osc2.next (inc2, 0.0, cur.osc[1], smoothCoef);
                pair = sLvl1 * o1 + sLvl2 * o2;
            }
            const float subOut = sSub > 1.0e-4f ? sub (inc1 * 0.5) : 0.0f;
            subPhase += inc1 * 0.5;
            if (subPhase >= 1.0) subPhase -= 1.0;

            const float osc = 0.3f * (pair + sSub * subOut);

            // Filter rack: slots in series, in the chosen order.
            const float fenv = filterEnv.next();
            const float lfo = cur.lfo[0] != nullptr ? cur.lfo[0][pos] : 0.0f;
            const float lfoOct = lfo * cur.lfoToCutoff * lfoOctavesAtFull; // older projects' "LFO to Cutoff"

            // Velocity level glides over about 2 ms, so a stolen or retriggered
            // voice that is still sounding doesn't jump to the new level (click).
            noteGainS += (noteGain - noteGainS) * gainGlideCoef;
            const float gain = ampEnv.next() * noteGainS;
            if (cur.taps[0] != nullptr) cur.taps[0][pos] += osc;

            // The chain: Filters 1-3, Amp and EQ in the chosen order.
            float x = osc;
            for (int n = 0; n < Chain::numBlocks; ++n)
            {
                const int b = cur.order[n];
                if (Chain::isFilter (b))
                {
                    auto slot = cur.slots[b];
                    const auto& mb = modNow[b];
                    slot.resonance = juce::jlimit (0.0f, 1.0f, sRes[b] + mb[1]);
                    slot.drive     = juce::jlimit (0.0f, 1.0f, sDrive[b] + mb[2]);
                    slot.character = juce::jlimit (0.0f, 1.0f, sChar[b] + mb[4]);
                    const float cutoffOct = sOct[b] + mb[0] + fenv * (sEnvAmt[b] + mb[3]) * envOctavesAtFull + lfoOct;
                    lastCutoffHz[b] = std::exp2 (juce::jlimit (4.3f, 14.3f, cutoffOct));

                    // Filter type changed mid-note: the old type carries on
                    // in a spare filter and fades out over 10 ms while the new
                    // one fades in, instead of the sound jumping (a click).
                    if (slot.type != lastType[b])
                    {
                        if (lastType[b] >= 0)
                        {
                            fadeFilters[b] = filters[b]; // same sizes: copies, never allocates
                            fadeType[b] = lastType[b];
                            typeFadeLeft[b] = typeFadeLen;
                            filters[b].reset();
                        }
                        lastType[b] = slot.type;
                    }
                    float y = filters[b].process (x, slot, lastCutoffHz[b]);
                    if (typeFadeLeft[b] > 0)
                    {
                        auto old = slot;
                        old.type = fadeType[b];
                        const float yOld = fadeFilters[b].process (x, old, lastCutoffHz[b]);
                        const float t = (float) typeFadeLeft[b] / (float) typeFadeLen;
                        const float fadeIn = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - t));
                        y = yOld + (y - yOld) * fadeIn;
                        --typeFadeLeft[b];
                    }
                    x = y;
                }
                else if (b == Chain::amp)
                {
                    x *= gain;
                }
                else if (! eq.flat)
                {
                    x = eq.process (x);
                }
                // The Numbers view shows each stage as it really is, so the
                // Amp column is where the envelope and velocity take effect.
                if (cur.taps[n + 1] != nullptr) cur.taps[n + 1][pos] += x;
            }
            lastFreq = (float) freq1;

            if (! std::isfinite (x))
            {
                // Something produced a broken number: clear this voice's
                // filters, EQ and smoothing so it recovers instead of going
                // silent for good.
                recoverFromBadValue();
                x = 0.0f;
            }
            // DC blocker (5 Hz high-pass): drive, resonance and ring mod can
            // push the wave off-centre, which wastes headroom and thumps.
            const float blocked = x - dcX1 + dcCoef * dcY1;
            dcX1 = x;
            dcY1 = blocked;
            const float sample = blocked;

            // Stereo Spread: place the voice left or right. The bass (below
            // about 150 Hz) stays in the centre, so the low end is solid and
            // nothing is lost when the mix is played in mono.
            float left = sample, right = sample;
            if (panActive)
            {
                if (panMoving)
                {
                    panLS += (panL - panLS) * panCoef;
                    panRS += (panR - panRS) * panCoef;
                    if (std::abs (panLS - panL) + std::abs (panRS - panR) < 1.0e-6f) { panLS = panL; panRS = panR; panMoving = false; }
                }
                bassS += (sample - bassS) * bassCoef;
                const float upper = sample - bassS;
                left  = bassS + upper * panLS;
                right = bassS + upper * panRS;
            }
            else
                bassS = sample; // ready to take over without a jump

            if (numChannels >= 2)
            {
                output.addSample (0, pos, left);
                output.addSample (1, pos, right);
                for (int ch = 2; ch < numChannels; ++ch)
                    output.addSample (ch, pos, sample);
            }
            else if (numChannels == 1)
                output.addSample (0, pos, sample);

            if (! ampEnv.isActive())
            {
                filterEnv.reset();
                clearCurrentNote();
                break;
            }
        }

        publishLiveInfo();
    }

    // Stop at once and forget everything (DAW reset, sample-rate change).
    void hardReset()
    {
        ampEnv.reset();
        filterEnv.reset();
        recoverFromBadValue();
        noteGainS = noteGain;
        bendS = bendSemitones;
        oscOversample = false;
        fmLp12[0] = fmLp12[1] = fmLp21[0] = fmLp21[1] = 0.0f;
        if (isVoiceActive())
            clearCurrentNote();
    }

private:
    void recoverFromBadValue()
    {
        osc1.reset (shared.osc[0]);
        osc2.reset (shared.osc[1]);
        subPhase = 0.0;
        for (auto& f : filters)
            f.reset();
        for (auto& n : typeFadeLeft)
            n = 0;
        eq.reset();
        sSub = shared.subLevel;
        sFm12 = shared.fm1to2; sFm21 = shared.fm2to1; sRing = shared.ringMod;
        sLvl1 = shared.osc[0].level; sLvl2 = shared.osc[1].level;
        for (auto& d : drift)
            d.value = 0.0f;
        bassS = 0.0f;
        snapPan = true;
        dcX1 = dcY1 = 0.0f;
        oscDown1.reset(); oscDown2.reset();
        snapSmoothing = true; // the next 16-sample update puts the rest back to the knob values
    }

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
            out.curveRatio.store ((float) e.curveRatioNow());
        };
        publish (live->filterEnv, filterEnv, cur.fenvA, cur.fenvD, cur.fenvR);
        publish (live->ampEnv, ampEnv, cur.aenvA, cur.aenvD, cur.aenvR);
    }

    void applyEnvelopeSettings()
    {
        // Voice Variation: each voice's envelopes run a little faster or slower.
        if (cur.variation > 0.0f)
        {
            const float a = std::exp2 (cur.variation * tol[tolEnvA] * varEnvOct);
            const float d = std::exp2 (cur.variation * tol[tolEnvD] * varEnvOct);
            const float r = std::exp2 (cur.variation * tol[tolEnvR] * varEnvOct);
            ampEnv.setParameters    (cur.aenvA * a, cur.aenvD * d, cur.aenvS, cur.aenvR * r, cur.aenvCurve);
            filterEnv.setParameters (cur.fenvA * a, cur.fenvD * d, cur.fenvS, cur.fenvR * r, cur.fenvCurve);
            return;
        }
        ampEnv.setParameters    (cur.aenvA, cur.aenvD, cur.aenvS, cur.aenvR, cur.aenvCurve);
        filterEnv.setParameters (cur.fenvA, cur.fenvD, cur.fenvS, cur.fenvR, cur.fenvCurve);
    }

    // An oscillator's tuning in semitones: its knobs plus this voice's own small offset.
    float tuneTarget (int o) const
    {
        return cur.osc[o].tuneSemitones() + cur.variation * tol[o == 0 ? tolTune1 : tolTune2] * varTuneCents * 0.01f;
    }

    // This voice's source values right now.
    void currentSources (int pos, float* src) const
    {
        src[Mod::lfo1]      = shared.lfo[0] != nullptr ? shared.lfo[0][pos] : 0.0f;
        src[Mod::lfo2]      = shared.lfo[1] != nullptr ? shared.lfo[1][pos] : 0.0f;
        src[Mod::filterEnv] = filterEnv.level();
        src[Mod::ampEnv]    = ampEnv.level();
        src[Mod::velocity]  = noteVelocity;
        src[Mod::modWheel]  = shared.mod != nullptr ? shared.mod->modWheelAt (pos) : 0.0f;
        src[Mod::key]       = juce::jlimit (-1.0f, 1.0f, (float) (targetNote - 60) / 60.0f);
    }

    void refreshSettings (int pos)
    {
        cur = shared;
        for (int s = 0; s < 3; ++s)
            knobSlots[s] = cur.slots[s];
        auto* m = shared.mod;
        if (m != nullptr)
        {
            if (m->anyRamping) // DAW automation glides across the block
                for (int t = 0; t < Mod::numTargets; ++t)
                    if (m->ramping[(size_t) t])
                        applyTarget (cur, t, m->baseRealAt (t, pos));
            for (int s = 0; s < 3; ++s)
                knobSlots[s] = cur.slots[s]; // before modulation

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
                        applyTarget (cur, t, Mod::applyOffset (*m, t, offset[t], pos));
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
    FilterSlot fadeFilters[3];          // the old type while a type change fades
    int lastType[3] { -1, -1, -1 }, fadeType[3] {}, typeFadeLeft[3] {}, typeFadeLen = 480;
    VoiceEq eq;

    int    targetNote    = 60;
    double currentNote   = 60.0;
    double glideStep     = 0.0;
    bool   gliding       = false;
    double bendSemitones = 0.0, bendS = 0.0;
    Oscillator osc1, osc2;
    double subPhase = 0.0;
    float  noteGain = 1.0f, noteGainS = 1.0f;
    bool   oscOversample = false;
    bool   needsRefresh = true;
    float  baseOct[3] { 10.0f, 10.0f, 10.0f }; // cutoff targets in octaves, kept between blocks   // FM / ring mod: oscillators at 4x
    Halfband oscDown1, oscDown2;
    float  smoothCoef4x = 0.001f, fmLpCoef = 0.4f;
    float  fmLp12[2] {}, fmLp21[2] {}; // FM signal softening (two gentle low-pass stages at 4x)
    float  dcX1 = 0.0f, dcY1 = 0.0f; // DC blocker state
    int    noteSerial = 0;
    float  lastFreq = 0.0f;
    float  lastCutoffHz[3] = { 1000.0f, 1000.0f, 1000.0f };

    float smoothCoef = 0.01f, blockSmoothCoef = 0.2f;
    bool  snapSmoothing = true;
    float sOct[3] { 10.0f, 10.0f, 10.0f }, sRes[3] {}, sDrive[3] {}, sEnvAmt[3] {}, sChar[3] {};
    // Filter modulation, kept apart from the knob smoothing: cutoff (octaves),
    // resonance, drive, env amount, type knob.
    static constexpr int numSlotMods = 5;
    FilterSlotSettings knobSlots[3];
    float modNow[3][numSlotMods] {}, modStep[3][numSlotMods] {};
    int modStepsLeft = 0;
    float sTune1 = 0.0f, sTune2 = 0.0f;
    float sEq[3] {};
    float sSub = 0.0f, sFm12 = 0.0f, sFm21 = 0.0f, sRing = 0.0f, sLvl1 = 1.0f, sLvl2 = 0.0f;

    struct Drift { float value = 0.0f, target = 0.0f; int counter = 0; };
    Drift drift[2];                  // one per oscillator
    float driftCoef = 0.0001f;

    // Voice Variation: this voice's fixed offsets, each -1..1.
    enum { tolTune1, tolTune2, tolCutoff, tolEnvA, tolEnvD, tolEnvR, tolLevel, numTol };
    float tol[numTol] {};

    // Stereo Spread
    float panPos = 0.0f;             // -1 left .. 1 right, before Spread
    int   panNote = -1;              // the note it was placed for
    float panL = 1.0f, panR = 1.0f, panLS = 1.0f, panRS = 1.0f, panCoef = 0.01f;
    float bassS = 0.0f, bassCoef = 0.02f; // the part kept in the centre
    bool  snapPan = true, panMoving = false, panActive = false;
};
