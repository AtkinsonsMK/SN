#pragma once

#include <cmath>

// ADSR with analogue-style exponential curves (like a capacitor charging and
// discharging). Attack aims slightly past full level so it rises with the
// characteristic "bowed" shape; decay and release fall exponentially and
// reach their target within the set time.
class Envelope
{
public:
    void setSampleRate (double sr) { sampleRate = sr; }

    void setParameters (float attackSec, float decaySec, float sustainLevel, float releaseSec)
    {
        sustain = sustainLevel;
        coefficients (attackSec,  attackRatio, attackCoef,  attackBase);
        attackBase = (1.0 + attackRatio) * (1.0 - attackCoef);

        coefficients (decaySec,   decayRatio,  decayCoef,   decayBase);
        decayBase = (sustain - decayRatio) * (1.0 - decayCoef);

        coefficients (releaseSec, decayRatio,  releaseCoef, releaseBase);
        releaseBase = -decayRatio * (1.0 - releaseCoef);
    }

    // Retriggering starts from the current level (no click), as analogue envelopes do.
    void noteOn()  { stage = Stage::attack; }
    void noteOff() { if (stage != Stage::idle) stage = Stage::release; }
    void reset()   { stage = Stage::idle; value = 0.0; }

    bool isActive() const { return stage != Stage::idle; }

    float next()
    {
        switch (stage)
        {
            case Stage::attack:
                value = attackBase + value * attackCoef;
                if (value >= 1.0) { value = 1.0; stage = Stage::decay; }
                break;

            case Stage::decay:
                value = decayBase + value * decayCoef;
                if (value <= sustain) { value = sustain; stage = Stage::sustain; }
                break;

            case Stage::sustain:
                value = sustain;
                break;

            case Stage::release:
                value = releaseBase + value * releaseCoef;
                if (value <= 0.0) { value = 0.0; stage = Stage::idle; }
                break;

            case Stage::idle:
                break;
        }
        return (float) value;
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void coefficients (float seconds, double ratio, double& coef, double& base) const
    {
        const double samples = std::max (1.0, (double) seconds * sampleRate);
        coef = std::exp (-std::log ((1.0 + ratio) / ratio) / samples);
        base = 0.0;
    }

    static constexpr double attackRatio = 0.3;
    static constexpr double decayRatio  = 0.0001;

    double sampleRate = 44100.0;
    Stage  stage = Stage::idle;
    double value = 0.0;
    double sustain = 1.0;
    double attackCoef = 0.0, attackBase = 0.0;
    double decayCoef = 0.0,  decayBase = 0.0;
    double releaseCoef = 0.0, releaseBase = 0.0;
};
