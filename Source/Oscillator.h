#pragma once

#include <cmath>

// Settings for one oscillator, in working units.
struct OscSettings
{
    float sine = 0.0f, saw = 1.0f, pulse = 0.0f;  // wave levels, 0..1
    float pulseWidth = 0.5f;                      // 0.05..0.95
    float level = 1.0f;                           // 0..1, how much goes to the mix
    float coarse = 0.0f;                          // semitones (whole steps)
    float fine = 0.0f;                            // cents (100 = one semitone)

    float tuneSemitones() const { return std::round (coarse) + fine * 0.01f; }
};

// One band-limited oscillator mixing sine, saw and pulse. Its phase can be
// pushed around by another oscillator (phase modulation, which is how most
// "FM" synths do FM, because it stays in tune).
class Oscillator
{
public:
    void reset (const OscSettings& s)
    {
        phase = 0.0;
        sSine = s.sine; sSaw = s.saw; sPulse = s.pulse; sPw = s.pulseWidth;
        lastOut = 0.0f;
    }

    // inc: cycles per sample. pm: phase offset in cycles from FM.
    float next (double inc, double pm, const OscSettings& s, float smooth)
    {
        sSine  += (s.sine       - sSine)  * smooth;
        sSaw   += (s.saw        - sSaw)   * smooth;
        sPulse += (s.pulse      - sPulse) * smooth;
        sPw    += (s.pulseWidth - sPw)    * smooth;

        double p = phase + pm;
        p -= std::floor (p);

        float out = 0.0f;
        if (sSine > 1.0e-4f)
            out += sSine * (float) std::sin (2.0 * 3.14159265358979 * p);
        if (sSaw > 1.0e-4f)
            out += sSaw * (float) (2.0 * p - 1.0 - polyBlep (p, inc));
        if (sPulse > 1.0e-4f)
        {
            double v = p < sPw ? 1.0 : -1.0;
            v += polyBlep (p, inc);
            double t = p - sPw;
            if (t < 0.0) t += 1.0;
            v -= polyBlep (t, inc);
            out += sPulse * (float) (v - (2.0 * sPw - 1.0)); // remove DC so level stays even
        }

        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;

        lastOut = out;
        return out;
    }

    double getPhase() const { return phase; }
    float  getLastOut() const { return lastOut; }

    // PolyBLEP: smooths each jump in the waveform so it doesn't alias (sound harsh).
    static double polyBlep (double t, double dt)
    {
        if (dt <= 0.0) return 0.0;
        if (t < dt)       { t /= dt;            return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

private:
    double phase = 0.0;
    float sSine = 0.0f, sSaw = 1.0f, sPulse = 0.0f, sPw = 0.5f;
    float lastOut = 0.0f;
};
