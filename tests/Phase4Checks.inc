// Phase 4 checks (engineering review): the look-ahead true-peak limiter and
// the synth resting when silent. Included by RenderTest.cpp.

namespace Phase4Checks
{
    // Independent true-peak meter: 16x windowed-sinc interpolation, 64 taps.
    static float truePeak (const std::vector<float>& x, size_t from)
    {
        float tp = 0.0f;
        const int half = 32;
        for (size_t n = from + (size_t) half; n + (size_t) half < x.size(); ++n)
        {
            tp = std::max (tp, std::abs (x[n]));
            for (int f = 1; f < 16; ++f)
            {
                const double frac = f / 16.0;
                double y = 0.0;
                for (int k = -half + 1; k <= half; ++k)
                {
                    const double u = frac - k;
                    const double w = 0.42 + 0.5 * std::cos (M_PI * u / half) + 0.08 * std::cos (2.0 * M_PI * u / half);
                    y += x[(size_t) ((long) n + k)] * std::sin (M_PI * u) / (M_PI * u) * w;
                }
                tp = std::max (tp, (float) std::abs (y));
            }
        }
        return tp;
    }

    // Level of a frequency (Goertzel with a Hann window), linear.
    static double toneLevel (const std::vector<float>& x, size_t from, size_t len, double hz, double sr)
    {
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (len - 1));
            const double ph = 2.0 * M_PI * hz * (double) i / sr;
            re += x[from + i] * w * std::cos (ph);
            im -= x[from + i] * w * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    // The limiter this phase replaced: instant attack on the stored samples.
    static std::vector<float> oldLimiter (const std::vector<float>& in, double sr)
    {
        std::vector<float> out (in.size());
        const float rel = std::exp (-1.0f / (0.15f * (float) sr));
        float env = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
        {
            env = std::max (std::abs (in[i]), env * rel);
            out[i] = env > TruePeakLimiter::ceiling ? in[i] * TruePeakLimiter::ceiling / env : in[i];
        }
        return out;
    }

    static std::vector<float> newLimiter (const std::vector<float>& in, double sr, int* latencyOut = nullptr)
    {
        TruePeakLimiter lim;
        lim.prepare (sr);
        if (latencyOut != nullptr) *latencyOut = lim.latency();
        std::vector<float> out (in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            float f[2] = { in[i], in[i] };
            lim.process (f, 2, true);
            out[i] = f[0];
        }
        return out;
    }

    static double harmonicsDb (const std::vector<float>& y, double f0, double sr)
    {
        const size_t from = (size_t) (0.5 * sr), len = (size_t) (0.4 * sr);
        const double fund = toneLevel (y, from, len, f0, sr);
        double h = 0.0;
        for (int k = 2; k <= 9; ++k) { const double a = toneLevel (y, from, len, f0 * k, sr); h += a * a; }
        return 10.0 * std::log10 (h / (fund * fund) + 1.0e-30);
    }

    static void limiter (double sr)
    {
        // 1. A loud bass note: the old limiter bent every cycle of the wave.
        {
            const double f0 = 55.0;
            std::vector<float> x ((size_t) sr);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 2.0f * (float) std::sin (2.0 * M_PI * f0 * (double) i / sr);
            const double before = harmonicsDb (oldLimiter (x, sr), f0, sr);
            const double after  = harmonicsDb (newLimiter (x, sr), f0, sr);
            check (after < -50.0 && after < before - 10.0,
                   "Limiter: loud bass stays clean (distortion " + juce::String (after, 1) + " dB, old limiter " + juce::String (before, 1) + " dB, target -50)");
        }

        // 2. Peaks between samples: a high tone whose samples miss its crests.
        {
            std::vector<float> x ((size_t) sr);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 1.2f * (float) std::sin (2.0 * M_PI * (sr / 4.0) * (double) i / sr + M_PI / 4.0); // samples at 0.85, crests at 1.2
            const float before = truePeak (oldLimiter (x, sr), (size_t) (0.5 * sr));
            const float after  = truePeak (newLimiter (x, sr), (size_t) (0.5 * sr));
            check (after <= 1.0f,
                   "Limiter: peaks between samples are caught (true peak " + juce::String (juce::Decibels::gainToDecibels (after), 2)
                   + " dBFS, old limiter " + juce::String (juce::Decibels::gainToDecibels (before), 2) + " dBFS, target 0)");
        }

        // 3. A sudden loud hit after silence: no sample gets over the ceiling.
        {
            std::vector<float> x ((size_t) (0.5 * sr), 0.0f);
            juce::Random r (7);
            for (size_t i = x.size() / 2; i < x.size(); ++i) x[i] = (r.nextFloat() * 2.0f - 1.0f) * 6.0f;
            int lat = 0;
            const auto y = newLimiter (x, sr, &lat);
            float peak = 0.0f;
            for (auto v : y) peak = std::max (peak, std::abs (v));
            check (peak <= TruePeakLimiter::ceiling * 1.0001f,
                   "Limiter: a sudden loud hit stays under -0.3 dBFS (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
            const double ms = 1000.0 * lat / sr;
            check (ms > 1.0 && ms < 2.0, "Limiter: look-ahead delay is short (" + juce::String (ms, 2) + " ms, reported to the host)");
        }

        // 4. Quiet sound passes untouched (just delayed).
        {
            std::vector<float> x ((size_t) (0.2 * sr));
            for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * (float) std::sin (0.05 * (double) i);
            int lat = 0;
            const auto y = newLimiter (x, sr, &lat);
            float worst = 0.0f;
            for (size_t i = (size_t) lat; i < x.size(); ++i) worst = std::max (worst, std::abs (y[i] - x[i - (size_t) lat]));
            check (! (worst > 0.0f), "Limiter: quiet sound passes untouched");
        }
    }

    // The drums' filters sleep when silent and wake cleanly: a hit after a
    // rest is as loud as the first one. (Drum hits carry a little noise, so
    // two hits are never sample-identical; their level is compared.)
    static void drumRest (double sr)
    {
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::drum (Drums::kick, ParamIDs::drumRoute), 1.0f);
        setParam (*p, ParamIDs::slot (0, ParamIDs::slotResonance), 85.0f);
        auto hit = [&]
        {
            std::vector<float> y;
            juce::AudioBuffer<float> buf (2, 512);
            p->auditionDrum (Drums::kick);
            for (int b = 0; b < 94; ++b)
            {
                juce::MidiBuffer none;
                p->processBlock (buf, none);
                for (int i = 0; i < 512; ++i) y.push_back (buf.getSample (0, i));
            }
            return y;
        };
        // Settle the level smoothing first, then compare two hits.
        hit();
        const auto a = hit();
        const bool rested = p->drumsAreResting();
        const auto b = hit();
        double ea = 0.0, eb = 0.0;
        for (size_t i = 0; i < a.size(); ++i) { ea += (double) a[i] * a[i]; eb += (double) b[i] * b[i]; }
        const double db = 10.0 * std::log10 ((eb + 1.0e-30) / (ea + 1.0e-30));
        check (rested && ea > 1.0e-3 && std::abs (db) < 0.5,
               "Drums: filters rest when silent and wake cleanly (rested " + juce::String (rested ? "yes" : "no")
               + ", next hit level " + juce::String (db, 2) + " dB)");
    }

    // The screen redraws only what changed.
    static void redraws (double sr)
    {
        auto p = makeSynth (sr);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        auto* ed = dynamic_cast<SNSynthEditor*> (editor.get());
        if (ed == nullptr) { check (false, "Screen: editor opens"); return; }
        juce::AudioBuffer<float> buf (2, 512);
        auto play = [&] (juce::MidiBuffer m) { p->processBlock (buf, m); };
        ed->tickForChecks();
        ed->tickForChecks();
        const int idle = ed->tickForChecks();          // nothing playing, nothing changed
        setParam (*p, ParamIDs::volume, -10.0f);
        const int afterChange = ed->tickForChecks();   // a setting moved
        const int settled = ed->tickForChecks();
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        play (on);
        ed->tickForChecks();
        play ({});
        const int playing = ed->tickForChecks();       // a note sounding
        check (idle <= 2 && settled <= 2 && afterChange > 20 && playing > idle && playing < afterChange,
               "Screen: redraws only what changed (idle " + juce::String (idle) + ", after a change " + juce::String (afterChange)
               + ", while a note plays " + juce::String (playing) + " parts)");
    }

    static void run (double sr)
    {
        redraws (sr);
        drumRest (sr);
        limiter (sr);
        limiter (96000.0);
    }
}
