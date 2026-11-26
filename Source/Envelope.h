#pragma once

#include <cmath>

// ADSR with analogue-style exponential curves (like a capacitor charging and
// discharging). Attack aims slightly past full level so it rises with the
// characteristic "bowed" shape; decay and release fall exponentially and
// reach their target within the set time.
class Envelope
{
public:
    void setSampleRate (double sr)
    {
        sampleRate = sr;
        sustainGlide = 1.0 - std::exp (-1.0 / (0.005 * sr)); // 5 ms
    }

    // curve: -1..1. 0 is the classic analogue shape; towards -1 decay and
    // release become straighter lines, towards +1 they drop away faster at
    // first with a longer soft tail. Decay and Release times are when the
    // level has fallen 99% of the way (-40 dB), whatever the curve.
    void setParameters (float attackSec, float decaySec, float sustainLevel, float releaseSec, float curve = 0.0f)
    {
        sustain = sustainLevel;
        coefficients (attackSec,  attackRatio, attackCoef,  attackBase);
        attackBase = (1.0 + attackRatio) * (1.0 - attackCoef);

        if (std::abs (curve - lastCurve) > 1.0e-6f)
        {
            lastCurve = curve;
            ratio = curveRatio (curve);
            ratioStretch = stretch (ratio);
        }
        coefficients (decaySec * (float) ratioStretch, ratio, decayCoef, decayBase);
        decayBase = (sustain - ratio) * (1.0 - decayCoef);

        coefficients (releaseSec * (float) ratioStretch, ratio, releaseCoef, releaseBase);
        releaseBase = -ratio * (1.0 - releaseCoef);
    }

    // How far past the target the decay and release curves aim: small =
    // strongly curved, large = nearly straight.
    static double curveRatio (float curve)
    {
        curve = std::min (1.0f, std::max (-1.0f, curve));
        return curve >= 0.0f ? std::pow (10.0, -4.0 - 2.0 * curve) : std::pow (10.0, -4.0 - 5.0 * curve);
    }

    // Full fall time / time to fall 99% of the way, for a curve ratio.
    static double stretch (double r)
    {
        return std::log (r / (1.0 + r)) / std::log ((0.01 + r) / (1.0 + r));
    }

    // Retriggering starts from the current level (no click), as analogue envelopes do.
    void noteOn()  { setStage (Stage::attack); }
    void noteOff() { if (stage != Stage::idle) setStage (Stage::release); }
    void reset()   { value = 0.0; setStage (Stage::idle); }

    bool isActive() const { return stage != Stage::idle; }

    // For the on-screen envelope display: 0 idle, 1 attack, 2 decay, 3 sustain, 4 release,
    // and how long the envelope has been in that stage.
    int    stageIndex() const      { return (int) stage; }
    double secondsInStage() const  { return samplesInStage / sampleRate; }
    float  level() const           { return (float) value; }
    float  stageStartLevel() const { return (float) startValue; } // where this stage began
    float  sustainLevel() const    { return (float) sustain; }

    // The exact curve the envelope follows, shared with the display so the
    // drawing and the sound always agree. Each stage is described by how far
    // through it you are (0..1): attack rises from its start level to 1,
    // decay falls from 1 to the sustain level, release falls from its start
    // level to 0. Stage numbers as stageIndex().
    static float curveLevel (int stageNum, float fraction, float startLevel, float sustainLvl, double decayRatio = classicRatio)
    {
        const double f = std::min (1.0, std::max (0.0, (double) fraction));
        switch (stageNum)
        {
            case 1: { const double d0 = 1.0 + attackRatio - startLevel;               // distance to the attack's aim
                      return (float) (1.0 + attackRatio - d0 * std::pow (attackRatio / d0, f)); }
            case 2: { const double d0 = 1.0 - sustainLvl + decayRatio;
                      return (float) (sustainLvl - decayRatio + d0 * std::pow (decayRatio / d0, f)); }
            case 3:   return sustainLvl;
            case 4: { const double d0 = std::max (0.0, (double) startLevel) + decayRatio;
                      return (float) (-decayRatio + d0 * std::pow (decayRatio / d0, f)); }
            default:  return 0.0f;
        }
    }

    // The reverse: how far through the stage a given level is (0..1).
    static float curveFraction (int stageNum, float levelNow, float startLevel, float sustainLvl, double decayRatio = classicRatio)
    {
        auto frac = [] (double d0, double dNow, double dEnd)
        {
            if (d0 <= dEnd * 1.000001) return 1.0f;
            return (float) std::min (1.0, std::max (0.0, std::log (d0 / std::max (dEnd, dNow)) / std::log (d0 / dEnd)));
        };
        switch (stageNum)
        {
            case 1:  return frac (1.0 + attackRatio - startLevel, 1.0 + attackRatio - levelNow, attackRatio);
            case 2:  return frac (1.0 - sustainLvl + decayRatio, levelNow - sustainLvl + decayRatio, decayRatio);
            case 4:  return frac (std::max (0.0, (double) startLevel) + decayRatio, levelNow + decayRatio, decayRatio);
            default: return 0.5f;
        }
    }

    // How long a stage really lasts, given its knob time: attack from a
    // non-zero level, decay to a high sustain, or release from a low level
    // all finish sooner than the knob time.
    static float stageSeconds (int stageNum, float knobSeconds, float startLevel, float sustainLvl, double decayRatio = classicRatio)
    {
        const float st = (float) stretch (decayRatio);
        auto share = [] (double d0, double dEnd, double dFull)
        {
            return d0 <= dEnd ? 0.0 : std::log (d0 / dEnd) / std::log (dFull / dEnd);
        };
        switch (stageNum)
        {
            case 1:  return knobSeconds * (float) share (1.0 + attackRatio - startLevel, attackRatio, 1.0 + attackRatio);
            case 2:  return knobSeconds * st * (float) share (1.0 - sustainLvl + decayRatio, decayRatio, 1.0 + decayRatio);
            case 4:  return knobSeconds * st * (float) share (std::max (0.0, (double) startLevel) + decayRatio, decayRatio, 1.0 + decayRatio);
            default: return 0.0f;
        }
    }

    float next()
    {
        ++samplesInStage;
        switch (stage)
        {
            case Stage::attack:
                value = attackBase + value * attackCoef;
                if (value >= 1.0) { value = 1.0; setStage (Stage::decay); }
                break;

            case Stage::decay:
                value = decayBase + value * decayCoef;
                // Reaching Sustain (or Sustain moved up past the level while
                // decaying): the sustain stage glides the last bit, so the
                // level never jumps.
                if (value <= sustain) setStage (Stage::sustain);
                break;

            case Stage::sustain:
                // Glide to a new Sustain level over a few ms instead of jumping (no click).
                value += (sustain - value) * sustainGlide;
                break;

            case Stage::release:
                value = releaseBase + value * releaseCoef;
                if (value <= 0.0) { value = 0.0; setStage (Stage::idle); }
                break;

            case Stage::idle:
                break;
        }
        return (float) value;
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void setStage (Stage s) { stage = s; samplesInStage = 0; startValue = value; }

    void coefficients (float seconds, double target, double& coef, double& base) const
    {
        const double samples = std::max (1.0, (double) seconds * sampleRate);
        coef = std::exp (-std::log ((1.0 + target) / target) / samples);
        base = 0.0;
    }

public:
    double curveRatioNow() const { return ratio; }
    static constexpr double classicRatio = 0.0001; // the shape before the Curve control (Curve 0)

private:
    static constexpr double attackRatio = 0.3;
    double ratio = classicRatio, ratioStretch = stretch (classicRatio);
    float lastCurve = 0.0f;

    double sampleRate = 44100.0;
    double sustainGlide = 0.005;
    Stage  stage = Stage::idle;
    double value = 0.0;
    double startValue = 0.0;
    double sustain = 1.0;
    double samplesInStage = 0.0;
    double attackCoef = 0.0, attackBase = 0.0;
    double decayCoef = 0.0,  decayBase = 0.0;
    double releaseCoef = 0.0, releaseBase = 0.0;
};
