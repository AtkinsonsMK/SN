#pragma once

#include <cmath>
#include <algorithm>

// The filter rack's analogue-style filter models.
//
// Each model runs at twice the sample rate internally (2x oversampling) so
// that Drive and resonance stay smooth instead of turning harsh.
//
// Types are numbered as in the Type switch. The order must never change,
// because FL Studio saves the switch position in projects.
namespace FilterType
{
    enum : int
    {
        off = 0,
        ota,
        sallenKey,
        lowpassGate,
        steinerParker, // later build
        polivoks,      // later build
        comb,          // later build
        formant,       // later build
        count
    };

    inline bool isAvailable (int t) { return t >= off && t <= lowpassGate; }
}

// Settings for one slot, already converted to working units.
struct FilterSlotSettings
{
    int   type = FilterType::off;
    float cutoffHz = 2000.0f;
    float resonance = 0.15f;   // 0..1
    float drive = 0.0f;        // 0..1
    float envAmount = 0.0f;    // -1..1
    float character = 0.0f;    // 0..1, the type-specific knob
};

namespace FilterMath
{
    constexpr float pi = 3.14159265358979f;

    // Fast, smooth saturation (close to tanh for small signals).
    inline float sat (float x)
    {
        x = std::clamp (x, -3.0f, 3.0f);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    // Gain for a cutoff, for the zero-delay-feedback (TPT) filter structures.
    inline float tptGain (float cutoffHz, float sampleRate)
    {
        const float fc = std::clamp (cutoffHz, 10.0f, sampleRate * 0.45f);
        return std::tan (pi * fc / sampleRate);
    }
}

// Settings shared by the sound and the on-screen filter curves.
namespace FilterShape
{
    inline float otaFeedback (float res)        { return res * 3.9f; }
    inline float otaDamping (float res)         { return 2.0f * (1.0f - res * 0.97f); }
    inline float sallenKeyDamping (float res)   { return 2.0f * (1.0f - res * 1.02f); }
    inline float gateDamping (float res)        { return 2.0f * (1.0f - res * 0.8f); }
    inline float sallenKeyHpHz (float ch)       { return 20.0f * std::pow (100.0f, ch); }  // 20 Hz to 2 kHz
    inline float gateDecaySeconds (float ch)    { return 0.12f * std::pow (25.0f, ch); }   // 0.12 s to 3 s
    inline bool  otaIs24dB (float ch)           { return ch < 0.5f; }
}

// One-pole low-pass / high-pass building block.
struct OnePole
{
    float s = 0.0f;
    void reset() { s = 0.0f; }

    float lowpass (float x, float g)
    {
        const float v = (x - s) * g / (1.0f + g);
        const float y = v + s;
        s = y + v;
        return y;
    }

    float highpass (float x, float g) { return x - lowpass (x, g); }
};

// State-variable filter (2-pole), with optional saturation of its internal
// state, which is what makes analogue 2-pole filters "scream" at high resonance.
struct SVF
{
    float s1 = 0.0f, s2 = 0.0f;
    void reset() { s1 = s2 = 0.0f; }

    // damping: 2 = no resonance, near 0 = self-oscillation.
    float lowpass (float x, float g, float damping, float stateLimit)
    {
        const float hp = (x - (damping + g) * s1 - s2) / (1.0f + damping * g + g * g);
        const float bp = g * hp + s1;
        s1 = g * hp + bp;
        const float lp = g * bp + s2;
        s2 = g * bp + lp;

        if (stateLimit > 0.0f)
            s1 = stateLimit * FilterMath::sat (s1 / stateLimit);

        return lp;
    }
};

// One slot of the rack, for one voice.
class FilterSlot
{
public:
    void prepare (float sampleRate)
    {
        osRate = sampleRate * 2.0f;
        gateAttackCoef = 1.0f - std::exp (-1.0f / (0.003f * sampleRate));
        reset();
    }

    void reset()
    {
        for (auto& p : poles) p.reset();
        for (auto& p : hpPoles) p.reset();
        svf.reset();
        ladderOut = 0.0f;
        previousInput = 0.0f;
        gateLevel = 0.0f;
        gateHoldSamples = 0;
    }

    // Called when a voice starts a note (the Lowpass Gate is "struck").
    void trigger (float sampleRate)
    {
        gateHoldSamples = (int) (0.005f * sampleRate);
    }

    // True while a Lowpass Gate in this slot is still open.
    float gate() const { return gateLevel; }

    bool gateIsOpen() const { return gateLevel > 1.0e-4f || gateHoldSamples > 0; }

    // Process one sample at the host rate.
    float process (float x, const FilterSlotSettings& s, float cutoffHz)
    {
        if (! FilterType::isAvailable (s.type) || s.type == FilterType::off)
            return x;

        float gateGain = 1.0f;
        if (s.type == FilterType::lowpassGate)
        {
            updateGate (s.character);
            // The gate closes the filter as well as the volume, the vactrol way.
            cutoffHz *= std::exp2 ((gateLevel - 1.0f) * 6.0f);
            gateGain = gateLevel * std::sqrt (gateLevel);
        }

        const float driveGain = 1.0f + s.drive * 15.0f;     // up to about +24 dB into the circuit
        const float makeup    = 1.0f / (1.0f + s.drive * 2.5f);
        const float g = FilterMath::tptGain (cutoffHz, osRate);

        // 2x oversampling: half-way sample, then the real one, averaged.
        const float a = step (0.5f * (previousInput + x) * driveGain, s, g);
        const float b = step (x * driveGain, s, g);
        previousInput = x;

        return 0.5f * (a + b) * makeup * gateGain * typeTrim (s.type);
    }

private:
    float step (float x, const FilterSlotSettings& s, float g)
    {
        using namespace FilterMath;

        switch (s.type)
        {
            case FilterType::ota:
            {
                if (FilterShape::otaIs24dB (s.character))
                {
                    // 24 dB: four cascaded stages with feedback around them,
                    // saturating at the input like an OTA ladder.
                    const float k = FilterShape::otaFeedback (s.resonance);
                    float u = sat (x - k * ladderOut);
                    for (auto& p : poles)
                        u = p.lowpass (u, g);
                    ladderOut = u;
                    return u * (1.0f + k * 0.45f); // keep the bass as resonance rises
                }

                // 12 dB: smooth 2-pole.
                const float damping = FilterShape::otaDamping (s.resonance);
                return svf.lowpass (sat (x), g, damping, 2.0f);
            }

            case FilterType::sallenKey:
            {
                // MS-20 style: high-pass first (the type knob), then an
                // aggressive 2-pole low-pass whose resonance can self-oscillate
                // and is held in check by saturation.
                const float hpHz = FilterShape::sallenKeyHpHz (s.character);
                const float hg = tptGain (hpHz, osRate);
                float v = hpPoles[0].highpass (x, hg);
                v = hpPoles[1].highpass (v, hg);
                const float damping = FilterShape::sallenKeyDamping (s.resonance);
                return svf.lowpass (sat (v * 1.2f), g, damping, 1.2f);
            }

            case FilterType::lowpassGate:
            {
                // Soft 2-pole with only gentle resonance.
                const float damping = FilterShape::gateDamping (s.resonance);
                return svf.lowpass (sat (x), g, damping, 0.0f);
            }

            default:
                return x;
        }
    }

    // Vactrol: opens quickly when struck, then closes slowly, more slowly
    // the closer it gets to shut (that's what gives the "bongo" decay).
    void updateGate (float character)
    {
        const float sr = osRate * 0.5f;
        if (gateHoldSamples > 0)
        {
            --gateHoldSamples;
            gateLevel += (1.0f - gateLevel) * gateAttackCoef;
            return;
        }

        const float decaySeconds = FilterShape::gateDecaySeconds (character);
        const float rate = (0.6f + 0.8f * gateLevel) / (decaySeconds * sr);
        gateLevel *= std::exp (-rate * 3.0f); // the sound has mostly faded after the set time
        if (gateLevel < 1.0e-5f)
            gateLevel = 0.0f;
    }

    // Per-type level trim so switching types doesn't jump in volume.
    static float typeTrim (int type)
    {
        switch (type)
        {
            case FilterType::sallenKey:   return 0.85f;
            case FilterType::lowpassGate: return 1.1f;
            default:                      return 1.0f;
        }
    }

    float osRate = 88200.0f;
    OnePole poles[4];
    OnePole hpPoles[2];
    SVF svf;
    float ladderOut = 0.0f;
    float previousInput = 0.0f;

    float gateLevel = 0.0f;
    float gateAttackCoef = 0.1f;
    int   gateHoldSamples = 0;
};
