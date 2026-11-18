#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// The drum machine: five synthesised drums (no samples) and a 16-step
// sequencer. Each drum is built from simple analogue-style parts: sine
// oscillators with falling pitch, filtered noise, and fast decays.
namespace Drums
{
    enum Drum { kick, snare, clap, hat, rim, numDrums };
    constexpr int numSteps = 16;

    inline const char* drumName (int d)
    {
        static const char* names[] = { "Kick", "Snare", "Clap", "Hat", "Rim" };
        return names[d];
    }

    // Parameter ID prefixes. Never rename.
    inline const char* drumId (int d)
    {
        static const char* ids[] = { "kick", "snare", "clap", "hat", "rim" };
        return ids[d];
    }

    // Where each drum goes: straight to the output, or into a filter slot
    // (it then runs through that filter and everything after it in the chain,
    // except the Amp, which belongs to the keyboard notes).
    enum Route { dry = 0, toFilter1, toFilter2, toFilter3, numRoutes };

    // Decay knob range for each drum, in seconds (0% to 100%).
    inline float decaySeconds (int d, float decay01)
    {
        static const float lo[] = { 0.08f, 0.06f, 0.08f, 0.025f, 0.012f };
        static const float hi[] = { 1.50f, 0.60f, 0.80f, 0.90f,  0.200f };
        return lo[d] * std::pow (hi[d] / lo[d], juce::jlimit (0.0f, 1.0f, decay01));
    }

    struct DrumSettings
    {
        float tune = 0.0f;    // semitones
        float decay = 0.5f;   // 0..1
        float tone = 0.5f;    // 0..1
        float level = 0.8f;   // 0..1
        int   route = dry;
    };

    // A small resonant filter (state variable) for the noise parts.
    struct Svf
    {
        float s1 = 0.0f, s2 = 0.0f, g = 0.1f, k = 1.0f;
        void set (float hz, float q, float sr)
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, sr * 0.45f, hz) / sr);
            k = 1.0f / q;
        }
        void reset() { s1 = s2 = 0.0f; }
        // Returns low-pass; band-pass and high-pass via the references.
        float run (float x, float& bp, float& hp)
        {
            hp = (x - (k + g) * s1 - s2) / (1.0f + k * g + g * g);
            bp = g * hp + s1;
            s1 = g * hp + bp;
            const float lp = g * bp + s2;
            s2 = g * bp + lp;
            return lp;
        }
        float lowpass (float x)  { float b, h; return run (x, b, h); }
        float bandpass (float x) { float b, h; run (x, b, h); return b; }
        float highpass (float x) { float b, h; run (x, b, h); return h; }
    };

    // One drum sound. Each drum plays one hit at a time; a new hit restarts it.
    class Voice
    {
    public:
        void prepare (int drumType, float sampleRate)
        {
            type = drumType;
            sr = sampleRate;
            active = false;
        }

        void trigger (const DrumSettings& s)
        {
            const float ratio = std::exp2 (s.tune / 12.0f);
            tone = s.tone;
            t = 0;
            active = true;
            const float decaySec = decaySeconds (type, s.decay);
            // Exponential fall that reaches -60 dB at the decay time.
            decayMul = std::exp (-6.9078f / (decaySec * sr));
            env = 1.0f;
            phase1 = phase2 = 0.0;
            n1.reset(); n2.reset();

            switch (type)
            {
                case kick:
                    baseHz = 50.0f * ratio;
                    sweep = 1.0f + 5.0f * tone;                      // starts up to 6x higher, then falls
                    sweepMul = std::exp (-1.0f / (0.028f * sr));
                    sweepEnv = 1.0f;
                    break;
                case snare:
                    baseHz = 185.0f * ratio;
                    bodyEnv = 1.0f;
                    bodyMul = std::exp (-6.9078f / (juce::jmin (0.25f, 0.06f + decaySec * 0.3f) * sr));
                    n1.set (1000.0f * ratio, 0.7f, sr);                 // noise high-pass
                    n2.set ((2500.0f + 9500.0f * tone) * ratio, 0.7f, sr); // noise brightness
                    break;
                case clap:
                    n1.set ((900.0f + 1400.0f * tone) * ratio, 2.2f, sr);
                    burstMul = std::exp (-1.0f / (0.0035f * sr));
                    burstEnv = 1.0f;
                    env = 0.0f; // the tail starts after the bursts
                    break;
                case hat:
                    baseHz = ratio;
                    n1.set ((5000.0f + 5000.0f * tone) * ratio, 0.9f, sr); // high-pass
                    n2.set ((5000.0f + 5000.0f * tone) * ratio, 0.9f, sr);
                    for (auto& p : hatPhases) p = 0.0;
                    break;
                case rim:
                    baseHz = 1700.0f * ratio;
                    bodyEnv = 1.0f;
                    bodyMul = std::exp (-6.9078f / (0.03f * sr));
                    n1.set (1700.0f * ratio, 6.0f, sr);
                    break;
                default: break;
            }
        }

        bool isActive() const { return active; }

        float next()
        {
            if (! active)
                return 0.0f;

            float out = 0.0f;
            const float secs = (float) t / sr;
            constexpr double twoPi = 6.283185307179586;

            switch (type)
            {
                case kick:
                {
                    const float hz = baseHz * (1.0f + (sweep - 1.0f) * sweepEnv);
                    sweepEnv *= sweepMul;
                    phase1 += hz / sr;
                    if (phase1 >= 1.0) phase1 -= 1.0;
                    float x = (float) std::sin (twoPi * phase1) * env;
                    // Tone also adds a short click at the start.
                    if (secs < 0.004f)
                        x += noise() * tone * 0.5f * (1.0f - secs / 0.004f);
                    out = std::tanh (1.6f * x) / std::tanh (1.6f);
                    break;
                }
                case snare:
                {
                    phase1 += baseHz / sr;           if (phase1 >= 1.0) phase1 -= 1.0;
                    phase2 += baseHz * 1.78f / sr;   if (phase2 >= 1.0) phase2 -= 1.0;
                    const float body = (float) (std::sin (twoPi * phase1) + 0.6 * std::sin (twoPi * phase2)) * 0.6f * bodyEnv;
                    bodyEnv *= bodyMul;
                    const float nz = n2.lowpass (n1.highpass (noise())) * env;
                    out = body * (1.0f - 0.5f * tone) + nz * (0.6f + 0.8f * tone);
                    break;
                }
                case clap:
                {
                    // Three quick bursts 10 ms apart (several hands), then a tail.
                    float burst = 0.0f;
                    if (secs < 0.03f)
                    {
                        const int sinceBurst = t % (int) (0.010f * sr);
                        if (sinceBurst == 0)
                            burstEnv = 1.0f;
                        burst = burstEnv;
                        burstEnv *= burstMul;
                        if (secs >= 0.02f)
                            env = 0.7f; // the tail takes over from the last burst
                    }
                    const float shape = juce::jmax (burst, env);
                    out = n1.bandpass (noise()) * shape * 2.2f;
                    if (secs >= 0.02f)
                        env *= decayMul;
                    break;
                }
                case hat:
                {
                    // Six square waves at clashing, unrelated pitches (the
                    // classic analogue hat recipe), plus a little noise,
                    // high-passed so only the metallic sizzle remains.
                    static const float freqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                    float metal = 0.0f;
                    for (int i = 0; i < 6; ++i)
                    {
                        hatPhases[(size_t) i] += freqs[i] * baseHz * 2.0f / sr;
                        if (hatPhases[(size_t) i] >= 1.0) hatPhases[(size_t) i] -= 1.0;
                        metal += hatPhases[(size_t) i] < 0.5 ? 1.0f : -1.0f;
                    }
                    const float x = metal * (1.0f / 6.0f) + noise() * 0.5f;
                    out = n2.highpass (n1.highpass (x)) * env * 1.6f;
                    break;
                }
                case rim:
                {
                    phase1 += baseHz / sr;          if (phase1 >= 1.0) phase1 -= 1.0;
                    phase2 += baseHz * 0.29f / sr;  if (phase2 >= 1.0) phase2 -= 1.0;
                    const float high = (float) std::sin (twoPi * phase1) * env;
                    const float low  = (float) std::sin (twoPi * phase2) * bodyEnv;
                    bodyEnv *= bodyMul;
                    const float click = secs < 0.0015f ? n1.bandpass (noise()) * 0.6f : 0.0f;
                    out = high * (0.4f + 0.5f * tone) + low * (0.8f - 0.5f * tone) + click;
                    break;
                }
                default: break;
            }

            if (type != clap)
                env *= decayMul;
            ++t;
            if ((env < 1.0e-4f && t > (int) (0.03f * sr)) || t > (int) (3.0f * sr))
                active = false;
            return out;
        }

    private:
        float noise()
        {
            rng = rng * 1664525u + 1013904223u;
            return (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }

        int type = kick;
        float sr = 44100.0f;
        bool active = false;
        int t = 0;
        float tone = 0.5f, env = 0.0f, decayMul = 0.999f;
        float baseHz = 50.0f, sweep = 1.0f, sweepEnv = 0.0f, sweepMul = 0.99f;
        float bodyEnv = 0.0f, bodyMul = 0.99f, burstEnv = 0.0f, burstMul = 0.99f;
        double phase1 = 0.0, phase2 = 0.0;
        std::array<double, 6> hatPhases {};
        Svf n1, n2;
        uint32_t rng = 22222u;
    };

    // The pattern: one 16-bit row per drum (bit n = step n is on). Shared
    // between the screen (writes) and the audio (reads).
    struct Pattern
    {
        std::array<std::atomic<uint32_t>, numDrums> rows {};

        Pattern() { setDefault(); }

        bool get (int d, int step) const { return (rows[(size_t) d].load (std::memory_order_relaxed) >> step) & 1u; }
        void set (int d, int step, bool on)
        {
            auto& r = rows[(size_t) d];
            uint32_t v = r.load();
            while (! r.compare_exchange_weak (v, on ? (v | (1u << step)) : (v & ~(1u << step)))) {}
        }
        void toggle (int d, int step) { set (d, step, ! get (d, step)); }

        // A simple beat to start from: kick on every beat, snare and clap on
        // 2 and 4, hats on the off-beats.
        void setDefault()
        {
            auto bits = [] (std::initializer_list<int> steps) { uint32_t v = 0; for (int s : steps) v |= 1u << s; return v; };
            rows[kick].store  (bits ({ 0, 4, 8, 12 }));
            rows[snare].store (bits ({ 4, 12 }));
            rows[clap].store  (bits ({ 12 }));
            rows[hat].store   (bits ({ 2, 6, 10, 14 }));
            rows[rim].store   (bits ({ 7, 15 }));
        }

        // Saved with the project as "kick,snare,clap,hat,rim" in hex, e.g. "1111,1010,...".
        juce::String toString() const
        {
            juce::StringArray parts;
            for (auto& r : rows)
                parts.add (juce::String::toHexString ((int) r.load()));
            return parts.joinIntoString (",");
        }
        void fromString (const juce::String& text)
        {
            juce::StringArray parts;
            parts.addTokens (text, ",", "");
            if (parts.size() != numDrums)
                return;
            for (int d = 0; d < numDrums; ++d)
                rows[(size_t) d].store ((uint32_t) parts[d].getHexValue32() & 0xffffu);
        }
    };

    // The step clock. In the standalone app it runs from the Tempo knob; in
    // a DAW it follows the DAW's tempo and position, and plays while the DAW plays.
    class Sequencer
    {
    public:
        struct HostInfo { bool valid = false; bool playing = false; double bpm = 120.0; double ppq = 0.0; };

        void prepare (double sampleRate) { sr = sampleRate; running = false; }

        // Call once per block. Returns false if stopped.
        bool begin (bool playOn, double tempoBpm, float swing01, const HostInfo& host)
        {
            const bool wantRun = playOn && (! host.valid || host.playing);
            if (! wantRun)
            {
                running = false;
                return false;
            }

            const double bpm = host.valid ? host.bpm : tempoBpm;
            stepsPerSample = juce::jlimit (20.0, 400.0, bpm) / 60.0 * 4.0 / sr;
            swingDelay = 0.5 * juce::jlimit (0.0f, 1.0f, swing01); // up to half a step on the off-beats

            if (host.valid)
            {
                const double hostPos = host.ppq * 4.0;
                if (! running || std::abs (hostPos - pos) > 0.05)
                    jumpTo (hostPos);
            }
            else if (! running)
                jumpTo (0.0);

            following = host.valid;
            running = true;
            return true;
        }

        // Advance one sample. Returns the step (0..15) that starts now, or -1.
        int tick()
        {
            int fired = -1;
            if (pos + 1.0e-9 >= stepTime (nextStep))
            {
                fired = (int) (((nextStep % numSteps) + numSteps) % numSteps);
                ++nextStep;
            }
            pos += stepsPerSample;
            if (! following && nextStep >= 1024 && pos >= 1024.0) // keep the numbers small on long runs
            {
                nextStep -= 1024;
                pos -= 1024.0;
            }
            return fired;
        }

        bool isRunning() const { return running; }

    private:
        double stepTime (long long step) const { return (double) step + ((step & 1) ? swingDelay : 0.0); }

        void jumpTo (double p)
        {
            pos = p;
            nextStep = (long long) std::floor (p + 1.0e-9);
            if (stepTime (nextStep) < p - 1.0e-6)
                ++nextStep;
        }

        double sr = 44100.0, pos = 0.0, stepsPerSample = 0.0, swingDelay = 0.0;
        long long nextStep = 0;
        bool running = false, following = false;
    };
}
