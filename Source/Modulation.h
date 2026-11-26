#pragma once

#include "Parameters.h"
#include <array>
#include <atomic>

// Modulation: any source can move any knob, each with its own amount.
// The amounts live in a matrix (source x target). An amount of 0 means
// "not connected". Amounts are saved with the project, not automated.
namespace Mod
{
    enum Source { lfo1, lfo2, filterEnv, ampEnv, velocity, modWheel, key, numSources };

    inline const char* sourceName (int s)
    {
        static const char* names[] = { "LFO 1", "LFO 2", "Filter Env", "Amp Env", "Velocity", "Mod Wheel", "Key" };
        return names[s];
    }

    // Short IDs used when saving. Never rename.
    inline const char* sourceId (int s)
    {
        static const char* ids[] = { "lfo1", "lfo2", "fenv", "aenv", "vel", "wheel", "key" };
        return ids[s];
    }

    inline juce::Colour sourceColour (int s)
    {
        static const juce::uint32 c[] = { 0xff2f6fd6, 0xff7a4fd1, 0xffc98a00, 0xffc2307a, 0xff2e9a5a, 0xff138f8f, 0xff8c6239 };
        return juce::Colour (c[s]);
    }

    // LFOs and Key swing both ways (-1..1); the rest go 0..1.
    inline bool isBipolar (int s) { return s == lfo1 || s == lfo2 || s == key; }

    // Every knob that can be modulated. The order is internal only; saved
    // routings use the parameter ID.
    enum Target
    {
        osc1Sine, osc1Saw, osc1Pulse, osc1Pw, osc1Coarse, osc1Fine, osc1Level,
        osc2Sine, osc2Saw, osc2Pulse, osc2Pw, osc2Coarse, osc2Fine, osc2Level,
        sub, fm12, fm21, ring,
        fenvA, fenvD, fenvS, fenvR,
        aenvA, aenvD, aenvS, aenvR,
        drift, glide,
        f1Cutoff, f1Res, f1Drive, f1Env, f1Char,
        f2Cutoff, f2Res, f2Drive, f2Env, f2Char,
        f3Cutoff, f3Res, f3Drive, f3Env, f3Char,
        eqLow, eqMid, eqHigh, volume,
        lfo1Rate, lfo1Smooth, lfo2Rate, lfo2Smooth,
        numTargets
    };

    // Filter slot targets: control 0 Cutoff, 1 Resonance, 2 Drive, 3 Env, 4 Type knob.
    constexpr int filterTarget (int slot, int control) { return f1Cutoff + slot * 5 + control; }

    // Volume and the LFOs' own knobs act on the whole synth rather than on each note.
    constexpr bool isGlobal (int t) { return t >= volume; }

    // Cutoff and LFO Rate are moved in octaves: an amount of 100% covers the
    // whole range (10 octaves for Cutoff, about 8.6 for LFO Rate).
    constexpr bool isCutoff (int t) { return t == f1Cutoff || t == f2Cutoff || t == f3Cutoff; }
    constexpr bool isRate (int t)   { return t == lfo1Rate || t == lfo2Rate; }
    constexpr float cutoffOctavesAtFull = 10.0f;
    constexpr float rateOctavesAtFull = 8.64f; // 0.05 to 20 Hz
    constexpr bool isOctaveScaled (int t) { return isCutoff (t) || isRate (t); }
    constexpr float octavesAtFull (int t) { return isRate (t) ? rateOctavesAtFull : cutoffOctavesAtFull; }

    inline const std::array<juce::String, numTargets>& targetIds()
    {
        static const std::array<juce::String, numTargets> ids = []
        {
            namespace P = ParamIDs;
            std::array<juce::String, numTargets> a;
                        const char* fixed[] = {
                P::sineLevel, P::sawLevel, P::pulseLevel, P::pulseWidth, P::osc1Coarse, P::osc1Fine, P::osc1Level,
                P::osc2Sine, P::osc2Saw, P::osc2Pulse, P::osc2Pw, P::osc2Coarse, P::osc2Fine, P::osc2Level,
                P::subLevel, P::fm1to2, P::fm2to1, P::ringMod,
                P::fenvAttack, P::fenvDecay, P::fenvSustain, P::fenvRelease,
                P::aenvAttack, P::aenvDecay, P::aenvSustain, P::aenvRelease,
                P::drift, P::glide };
            int n = 0;
            for (auto* id : fixed)
                a[(size_t) n++] = id;
            for (int s = 0; s < P::numSlots; ++s)
                for (auto* c : { P::slotCutoff, P::slotResonance, P::slotDrive, P::slotEnv, P::slotCharacter })
                    a[(size_t) n++] = P::slot (s, c);
            a[(size_t) n++] = ParamIDs::eqLow;
            a[(size_t) n++] = ParamIDs::eqMid;
            a[(size_t) n++] = ParamIDs::eqHigh;
            a[(size_t) n++] = ParamIDs::volume;
            a[(size_t) n++] = ParamIDs::lfoRate;
            a[(size_t) n++] = ParamIDs::lfo1Smooth;
            a[(size_t) n++] = ParamIDs::lfo2Rate;
            a[(size_t) n++] = ParamIDs::lfo2Smooth;
            jassert (n == numTargets);
            return a;
        }();
        return ids;
    }

    inline int targetForParam (const juce::String& id)
    {
        const auto& ids = targetIds();
        for (int t = 0; t < numTargets; ++t)
            if (ids[(size_t) t] == id)
                return t;
        return -1;
    }

    // The routing amounts, shared between the screen (writes) and the
    // audio (reads). Each is -1..1 of the knob's full travel.
    struct Matrix
    {
        std::array<std::array<std::atomic<float>, numTargets>, numSources> amount {};

        Matrix() { clear(); }
        void clear()
        {
            for (auto& row : amount)
                for (auto& a : row)
                    a.store (0.0f);
        }
        float get (int s, int t) const { return amount[(size_t) s][(size_t) t].load (std::memory_order_relaxed); }
        void set (int s, int t, float v) { amount[(size_t) s][(size_t) t].store (juce::jlimit (-1.0f, 1.0f, v)); }

        // Saved as a child of the plugin state: <MODULATION> <ROUTE source="lfo1" target="f1_cutoff" amount="0.3"/> ...
        juce::ValueTree toValueTree() const
        {
            juce::ValueTree tree ("MODULATION");
            for (int s = 0; s < numSources; ++s)
                for (int t = 0; t < numTargets; ++t)
                    if (std::abs (get (s, t)) > 1.0e-5f)
                        tree.appendChild (juce::ValueTree ("ROUTE", { { "source", sourceId (s) },
                                                                      { "target", targetIds()[(size_t) t] },
                                                                      { "amount", get (s, t) } }), nullptr);
            return tree;
        }

        void fromValueTree (const juce::ValueTree& tree)
        {
            clear();
            for (const auto& route : tree)
            {
                int s = -1;
                for (int i = 0; i < numSources; ++i)
                    if (route["source"].toString() == sourceId (i))
                        s = i;
                const int t = targetForParam (route["target"].toString());
                if (s >= 0 && t >= 0)
                    set (s, t, (float) route["amount"]);
            }
        }
    };

    // What one block of audio needs: the active routes and the knob
    // positions before modulation. Filled by the processor once per block.
    struct Context
    {
        struct Route { int source; int target; float amount; };
        static constexpr int maxRoutes = numSources * numTargets;
        std::array<Route, maxRoutes> routes {};
        int numRoutes = 0;
        bool anyVoiceRoutes = false;

        std::array<float, numTargets> baseNorm {};  // 0..1 knob position
        std::array<float, numTargets> baseReal {};  // the value in its own units
        std::array<juce::NormalisableRange<float>, numTargets> ranges;

        // DAW automation arrives once per block. Each knob that moved glides
        // in a straight line from last block's value to this one across the
        // block, so automation never steps at large buffer sizes.
        std::array<float, numTargets> prevReal {}, prevNorm {};
        std::array<bool, numTargets> ramping {};
        bool anyRamping = false;
        float invBlockLen = 0.0f;

        float rampFrac (int pos) const { return juce::jlimit (0.0f, 1.0f, (float) (pos + 16) * invBlockLen); }
        float baseRealAt (int t, int pos) const;
        float baseNormAt (int t, int pos) const
        {
            if (pos < 0 || ! ramping[(size_t) t]) return baseNorm[(size_t) t];
            return prevNorm[(size_t) t] + (baseNorm[(size_t) t] - prevNorm[(size_t) t]) * rampFrac (pos);
        }

        float modWheel = 0.0f;              // 0..1
        const float* lfo[2] = { nullptr, nullptr };

        // The newest note's source values, for EQ and Volume.
        std::array<float, numSources> newestSources {};
    };

    // The modulated value of target t, given the summed offset (-1..1 range units).
    inline float Context::baseRealAt (int t, int pos) const
    {
        if (pos < 0 || ! ramping[(size_t) t]) return baseReal[(size_t) t];
        const float a = prevReal[(size_t) t], b = baseReal[(size_t) t], f = rampFrac (pos);
        if (isOctaveScaled (t)) // glide in octaves, as the ear hears it
            return std::exp2 (std::log2 (juce::jmax (1.0e-3f, a)) + (std::log2 (juce::jmax (1.0e-3f, b)) - std::log2 (juce::jmax (1.0e-3f, a))) * f);
        return a + (b - a) * f;
    }

    // pos: sample position in the block (for the automation glide); -1 = end value.
    inline float applyOffset (const Context& c, int t, float offset, int pos = -1)
    {
        const auto& r = c.ranges[(size_t) t];
        if (isOctaveScaled (t))
        {
            const float oct = std::log2 (juce::jmax (1.0e-3f, c.baseRealAt (t, pos))) + offset * octavesAtFull (t);
            return juce::jlimit (r.start, r.end, std::exp2 (oct));
        }
        return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, c.baseNormAt (t, pos) + offset));
    }
}
