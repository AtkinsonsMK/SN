#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// Output Limiter (engineering review Phase 4).
//
// The old limiter turned the level down on the very sample a peak arrived,
// which bends the waveform (audible as a crackle on loud bass) and only saw
// the stored samples, not the peaks a DAC rebuilds between them.
//
// This one:
//  1. Estimates the "true peak" between samples with a 4x interpolator
//     (12-tap windowed sinc per phase), like a loudness meter does.
//  2. Looks 1.5 ms ahead: the audio is delayed so the gain can start
//     falling before a peak arrives, as a smooth ramp instead of a step.
//  3. Holds the gain for 20 ms after a peak (longer than half a cycle of
//     a low bass note, so the gain doesn't wobble with the wave), then
//     lets go over about 150 ms.
// The gain ramp is a moving average (as long as the look-ahead) of a moving
// minimum (look-ahead plus hold), which guarantees the gain is fully down when the peak passes.
// The delay is always present (on or off) so the latency the host is told
// never changes.
class TruePeakLimiter
{
public:
    static constexpr float ceiling = 0.966f; // -0.3 dBFS
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate)
    {
        look = std::clamp ((int) std::lround (0.0015 * sampleRate), 8, maxLook);
        window = std::min (look + (int) std::lround (0.020 * sampleRate), minSize - 1);
        releaseCoef = (float) std::exp (-1.0 / (0.15 * sampleRate));
        if (! coefsReady) makeCoefs();
        reset();
    }

    void reset()
    {
        for (auto& h : hist) h.fill (0.0f);
        for (auto& d : delay) d.fill (0.0f);
        minT.fill (0); minV.fill (1.0f);
        box.fill (1.0f);
        minHead = minTail = 0;
        t = 0;
        released = 1.0f;
        boxSum = (double) look;
        boxPos = 0;
        histPos = 0;
        delayPos = 0;
        sinceResum = 0;
        restRun = 0;
    }

    // Samples of delay the limiter adds (report this to the host).
    int latency() const { return look + interpDelay - 1; }

    // Processes one sample frame in place. Returns the gain applied.
    float process (float* frame, int numChannels, bool on)
    {
        numChannels = std::min (numChannels, maxChannels);

        // Resting: after 512 silent samples with the gain fully back, every
        // store inside is zeros and ones, so the work can be skipped and the
        // output is exactly what it would have been (silence).
        bool silent = true;
        for (int ch = 0; ch < numChannels; ++ch) silent = silent && ! (std::abs (frame[ch]) > 0.0f);
        if (silent && released >= 1.0f)
        {
            if (restRun >= ringSize) return 1.0f;
            ++restRun;
        }
        else
            restRun = 0;

        // 1. True peak of this frame's segment (the samples interpDelay ago).
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& h = hist[(size_t) ch];
            h[(size_t) histPos] = h[(size_t) histPos + histLen] = frame[ch]; // mirrored ring: contiguous reads
            const float* x = h.data() + histPos + 1;                          // oldest .. newest
            peak = std::max (peak, std::abs (x[histLen - 1 - interpDelay]));
            for (int ph = 0; ph < 3; ++ph)
            {
                float y = 0.0f;
                for (int k = 0; k < histLen; ++k) y += x[k] * coefs[(size_t) ph][(size_t) k];
                peak = std::max (peak, std::abs (y));
            }
        }
        histPos = (histPos + 1) % histLen;

        // 2. Gain needed for this peak, then the lowest over the look-ahead
        //    plus the hold time.
        const float need = (on && peak > ceiling) ? ceiling / peak : 1.0f;
        pushMin (need);
        const float held = minV[(size_t) (minHead & minMask)];

        // 3. Release: rise back slowly, never above what's needed.
        released = std::min (held, 1.0f - (1.0f - released) * releaseCoef);

        // 4. Smooth the attack: average over the look-ahead.
        boxSum += (double) released - (double) box[(size_t) boxPos];
        box[(size_t) boxPos] = released;
        boxPos = (boxPos + 1) % look;
        if (++sinceResum >= 4096) // keep rounding from creeping in over hours
        {
            sinceResum = 0;
            boxSum = 0.0;
            for (int i = 0; i < look; ++i) boxSum += (double) box[(size_t) i];
        }
        const float gain = std::min (1.0f, (float) (boxSum / (double) look));

        // 5. Delay the audio to line up with the gain.
        const int d = latency();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& line = delay[(size_t) ch];
            line[(size_t) delayPos] = frame[ch];
            frame[ch] = line[(size_t) ((delayPos - d) & mask)] * gain;
        }
        delayPos = (delayPos + 1) & mask;
        ++t;
        return gain;
    }

private:
    static constexpr int maxLook = 400;           // 1.5 ms up to 266 kHz
    static constexpr int ringSize = 512;          // power of two > maxLook + interpDelay
    static constexpr int mask = ringSize - 1;
    static constexpr int minSize = 8192;          // > 21.5 ms at 384 kHz
    static constexpr int minMask = minSize - 1;
    static constexpr int histLen = 12;            // taps per phase
    static constexpr int interpDelay = histLen / 2;

    void makeCoefs()
    {
        // Values at 1/4, 2/4 and 3/4 of the way between the two middle samples.
        for (int ph = 0; ph < 3; ++ph)
        {
            const double frac = (ph + 1) / 4.0;
            double sum = 0.0;
            for (int k = 0; k < histLen; ++k)
            {
                const double u = (double) (histLen - 1 - interpDelay) + frac - (double) k; // distance from tap k
                const double sinc = std::abs (u) < 1.0e-9 ? 1.0 : std::sin (M_PI * u) / (M_PI * u);
                const double w = 0.5 * (1.0 + std::cos (M_PI * u / (histLen / 2 + 0.5)));
                coefs[(size_t) ph][(size_t) k] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (auto& c : coefs[(size_t) ph]) c = (float) (c / sum);
        }
        coefsReady = true;
    }

    void pushMin (float v)
    {
        // Moving minimum over the last `look` values (a monotonic queue).
        while (minTail != minHead && minV[(size_t) ((minTail - 1) & minMask)] >= v) --minTail;
        minV[(size_t) (minTail & minMask)] = v;
        minT[(size_t) (minTail & minMask)] = t;
        ++minTail;
        while (minT[(size_t) (minHead & minMask)] <= t - window) ++minHead;
    }

    int look = 72, window = 1032;
    float releaseCoef = 0.9999f, released = 1.0f;
    bool coefsReady = false;
    std::array<std::array<float, histLen>, 3> coefs {};
    std::array<std::array<float, histLen * 2>, maxChannels> hist {};
    int histPos = 0;
    std::array<std::array<float, ringSize>, maxChannels> delay {};
    int delayPos = 0;
    std::array<long long, minSize> minT {};
    std::array<float, minSize> minV {};
    long long minHead = 0, minTail = 0, t = 0;
    std::array<float, maxLook> box {};
    double boxSum = 0.0;
    int boxPos = 0, sinceResum = 0, restRun = 0;
};
