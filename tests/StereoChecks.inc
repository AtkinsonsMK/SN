// Stereo and "alive" checks (sound review items 11 and 13): Stereo Spread,
// drift per oscillator, Voice Variation, Start Phase and repeated notes.
// Included by RenderTest.cpp.

namespace StereoChecks
{
    using SoundChecks::Event;
    using SoundChecks::neutral;
    using SoundChecks::rms;
    using SoundChecks::renderEvents;

    struct Stereo { std::vector<float> l, r; };

    static Stereo renderStereo (SNSynthProcessor& p, const std::vector<Event>& events, int numSamples, int blockSize = 512)
    {
        Stereo out { std::vector<float> ((size_t) numSamples), std::vector<float> ((size_t) numSamples) };
        juce::AudioBuffer<float> block (2, blockSize);
        for (int pos = 0; pos < numSamples; pos += blockSize)
        {
            const int len = juce::jmin (blockSize, numSamples - pos);
            juce::MidiBuffer midi;
            for (auto& e : events)
                if (e.sample >= pos && e.sample < pos + len)
                    midi.addEvent (e.msg, e.sample - pos);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, len);
            p.processBlock (view, midi);
            std::copy (block.getReadPointer (0), block.getReadPointer (0) + len, out.l.begin() + pos);
            std::copy (block.getReadPointer (1), block.getReadPointer (1) + len, out.r.begin() + pos);
        }
        return out;
    }

    // Power below / above a frequency, by FFT (Blackman-Harris window).
    static double bandPower (const std::vector<float>& x, size_t from, double sr, double loHz, double hiHz)
    {
        constexpr int order = 15, W = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * W, 0.0f);
        for (int i = 0; i < W; ++i)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * i / W;
            const double w = 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2 * ph) - 0.01168 * std::cos (3 * ph);
            buf[(size_t) i] = (float) (x[from + (size_t) i] * w);
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double s = 0.0;
        for (int j = 1; j < W / 2; ++j)
        {
            const double f = j * sr / W;
            if (f >= loHz && f < hiHz) s += (double) buf[(size_t) j] * buf[(size_t) j];
        }
        return s;
    }

    // Frequency of the strongest peak near a guess (FFT, Gaussian interpolation).
    static double peakFreq (const std::vector<float>& x, size_t from, double sr, double guessHz)
    {
        constexpr int order = 16, W = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * W, 0.0f);
        for (int i = 0; i < W; ++i)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * i / W;
            const double w = 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2 * ph) - 0.01168 * std::cos (3 * ph);
            buf[(size_t) i] = (float) (x[from + (size_t) i] * w);
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        const int c = (int) std::round (guessHz * W / sr);
        int best = c;
        for (int j = c - 40; j <= c + 40; ++j) if (buf[(size_t) j] > buf[(size_t) best]) best = j;
        const double a = std::log (buf[(size_t) best - 1] + 1e-20), b = std::log (buf[(size_t) best] + 1e-20), g = std::log (buf[(size_t) best + 1] + 1e-20);
        const double d = 0.5 * (a - g) / (a - 2 * b + g);
        return (best + d) * sr / W;
    }

    // Pitch of a sine, every 50 ms, from zero crossings with sub-sample timing.
    static std::vector<double> pitchTrack (const std::vector<float>& x, size_t from, double sr)
    {
        std::vector<double> crossings;
        for (size_t i = from + 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back ((double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]));
        std::vector<double> hz;
        const double win = 0.05 * sr;
        size_t k = 0;
        while (k < crossings.size())
        {
            size_t e = k;
            while (e + 1 < crossings.size() && crossings[e + 1] - crossings[k] <= win) ++e;
            if (e <= k || crossings[k] + win > crossings.back()) break;
            hz.push_back ((double) (e - k) * sr / (crossings[e] - crossings[k]));
            k = e;
        }
        return hz;
    }

    static void run (double sr)
    {
        using juce::MidiMessage;
        const int sec = (int) sr;

        // A. Same note again while the old one is still releasing: the level
        //    must not depend on the exact timing (target: spread within 1 dB; was 4.8 dB).
        for (int phaseMode : { 0, 1 })
        {
            auto setup = [&] { auto q = neutral (sr);
                               setParam (*q, ParamIDs::startPhase, (float) phaseMode);
                               setParam (*q, ParamIDs::spread, 50); setParam (*q, ParamIDs::variation, 25);
                               setParam (*q, ParamIDs::aenvRelease, 2.0f); return q; };
            const int on2Base = (int) (0.3 * sr);
            auto single = renderEvents (*setup(), { { 0, MidiMessage::noteOn (1, 57, (juce::uint8) 100) } }, (int) (0.5 * sr));
            const double ref = rms (single, (size_t) (0.02 * sr), (size_t) (0.12 * sr));
            double lo = 1e9, hi = -1e9;
            juce::String list;
            for (double gapMs : { 1.0, 2.0, 3.0, 5.0, 8.0, 13.0, 20.0, 50.0 })
            {
                const int on2 = on2Base + (int) (gapMs * 0.001 * sr);
                auto x = renderEvents (*setup(), { { 0, MidiMessage::noteOn (1, 57, (juce::uint8) 100) },
                                                   { on2Base, MidiMessage::noteOff (1, 57) },
                                                   { on2, MidiMessage::noteOn (1, 57, (juce::uint8) 100) } }, on2 + (int) (0.15 * sr));
                const double db = 20.0 * std::log10 (rms (x, (size_t) (on2 + 0.02 * sr), (size_t) (on2 + 0.12 * sr)) / ref);
                lo = juce::jmin (lo, db); hi = juce::jmax (hi, db);
                list << juce::String (db, 1) << " ";
            }
            check (hi - lo <= 1.0, juce::String ("Stereo A: a repeated note has the same level whatever the timing, Start Phase ")
                                       + (phaseMode ? "Random" : "Reset") + " (spread " + juce::String (hi - lo, 2)
                                       + " dB, target 1; gaps 1-50 ms: " + list.trim() + ")");
        }

        // B. Stereo Spread: a chord fans out, the bass stays centred, nothing is lost in mono.
        {
            auto setup = [&] (float spread) { auto q = neutral (sr);
                                              setParam (*q, ParamIDs::spread, spread);
                                              setParam (*q, ParamIDs::startPhase, 1); setParam (*q, ParamIDs::variation, 25);
                                              return q; };
            std::vector<Event> chord;
            for (int n : { 36, 48, 52, 55, 59 })
                chord.push_back ({ 0, MidiMessage::noteOn (1, n, (juce::uint8) 100) });
            const int len = (int) (1.5 * sr);
            const size_t from = (size_t) (0.5 * sr);

            auto s0 = renderStereo (*setup (0), chord, len);
            const bool same = std::memcmp (s0.l.data(), s0.r.data(), s0.l.size() * sizeof (float)) == 0;
            check (same, "Stereo B1: Spread 0% is exactly mono (left = right)");

            auto mono = [] (const Stereo& s) { std::vector<float> m (s.l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.l[i] + s.r[i]); return m; };
            // Width is judged above 300 Hz: below that the bass is meant to stay centred.
            auto highPass = [&] (const std::vector<float>& x)
            {
                auto c = juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass (sr, 300.0, 0.7071);
                std::vector<double> y (x.size());
                double z1 = 0, z2 = 0;
                for (size_t i = 0; i < x.size(); ++i)
                {
                    const double out = (c[0] * x[i] + z1) / c[3];
                    z1 = (c[1] * x[i] - c[4] * out) / c[3] + z2;
                    z2 = (c[2] * x[i] - c[5] * out) / c[3];
                    y[i] = out;
                }
                return y;
            };
            auto corr = [&] (const Stereo& s) { const auto l = highPass (s.l), r = highPass (s.r);
                                                double lr = 0, ll = 0, rr = 0;
                                                for (size_t i = from; i < l.size(); ++i) { lr += l[i] * r[i]; ll += l[i] * l[i]; rr += r[i] * r[i]; }
                                                return lr / std::sqrt (ll * rr); };
            const auto m0 = mono (s0);
            for (float spread : { 50.0f, 100.0f })
            {
                auto s = renderStereo (*setup (spread), chord, len);
                const auto m = mono (s);
                const double c = corr (s);
                const double bassLoss = 10.0 * std::log10 (bandPower (m, from, sr, 20, 150) / bandPower (m0, from, sr, 20, 150));
                const double allLoss  = 10.0 * std::log10 (bandPower (m, from, sr, 20, 20000) / bandPower (m0, from, sr, 20, 20000));
                const double sideBass = 10.0 * std::log10 (bandPower (s.l, from, sr, 20, 80) / bandPower (s.r, from, sr, 20, 80));
                const bool widthOk = spread < 75.0f ? (c >= 0.3 && c <= 0.8) : c < 0.6;
                check (widthOk, "Stereo B2: Spread " + juce::String ((int) spread) + "% makes the chord wide (left-right correlation above 300 Hz "
                                    + juce::String (c, 2) + (spread < 75.0f ? ", target 0.3 to 0.8)" : ", target below 0.6)"));
                check (std::abs (bassLoss) <= 1.0, "Stereo B3: Spread " + juce::String ((int) spread) + "%, bass below 150 Hz keeps its level in mono ("
                                                       + juce::String (bassLoss, 2) + " dB, target within 1)");
                check (std::abs (allLoss) <= 3.0, "Stereo B4: Spread " + juce::String ((int) spread) + "%, whole sound keeps its level in mono ("
                                                      + juce::String (allLoss, 2) + " dB, target within 3)");
                check (std::abs (sideBass) <= 0.5, "Stereo B5: Spread " + juce::String ((int) spread) + "%, the low C stays in the centre (left vs right "
                                                       + juce::String (sideBass, 2) + " dB below 80 Hz, target within 0.5)");
            }
        }

        // C. Drift: each oscillator wanders on its own, by a musical amount.
        {
            auto track = [&] (int osc, float driftPct)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::drift, driftPct);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
                setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
                setParam (*q, ParamIDs::osc2Saw, 0); setParam (*q, ParamIDs::osc2Sine, 100);
                setParam (*q, ParamIDs::osc2Fine, 0); setParam (*q, ParamIDs::osc2Coarse, 0);
                setParam (*q, ParamIDs::osc1Level, osc == 0 ? 100.0f : 0.0f);
                setParam (*q, ParamIDs::osc2Level, osc == 1 ? 100.0f : 0.0f);
                auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, 69, (juce::uint8) 100) } }, 30 * sec);
                auto hz = pitchTrack (x, (size_t) sec, sr);
                std::vector<double> cents;
                for (double h : hz) cents.push_back (1200.0 * std::log2 (h / 440.0));
                return cents;
            };
            const auto defaultDrift = paramValue (*makeSynth (sr), ParamIDs::drift);
            auto a = track (0, defaultDrift), b = track (1, defaultDrift);
            const size_t n = juce::jmin (a.size(), b.size());
            double ma = 0, mb = 0;
            for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
            ma /= n; mb /= n;
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
            const double r = sab / std::sqrt (saa * sbb);
            const double rmsA = std::sqrt (saa / n), rmsB = std::sqrt (sbb / n);
            check (std::abs (r) <= 0.3, "Stereo C1: the two oscillators drift independently (correlation " + juce::String (r, 2) + ", target within 0.3)");
            check (rmsA >= 1.0 && rmsA <= 3.0 && rmsB >= 1.0 && rmsB <= 3.0,
                   "Stereo C2: default Drift (" + juce::String ((int) defaultDrift) + "%) wanders 1 to 3 cents RMS (osc 1 "
                       + juce::String (rmsA, 2) + ", osc 2 " + juce::String (rmsB, 2) + " cents)");
            auto z = track (0, 0);
            double zmax = 0; for (double c : z) zmax = juce::jmax (zmax, std::abs (c));
            check (zmax <= 0.05, "Stereo C3: Drift 0% stays exactly in tune (worst " + juce::String (zmax, 3) + " cents)");
        }

        // D. Voice Variation: each voice is a little different, within its stated range.
        {
            auto measure = [&] (float variation)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::variation, variation);
                setParam (*q, ParamIDs::voices, 16);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
                setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
                // One note per voice, far enough apart for the FFT to tell them apart.
                const int notes[] = { 45, 52, 57, 64, 69, 76, 81, 88 };
                std::vector<Event> ev;
                for (int nt : notes) ev.push_back ({ 0, MidiMessage::noteOn (1, nt, (juce::uint8) 100) });
                auto x = renderEvents (*q, ev, 2 * sec);
                double worst = 0.0, spreadMax = 0.0;
                for (int nt : notes)
                {
                    const double f0 = 440.0 * std::pow (2.0, (nt - 69) / 12.0);
                    const double c = 1200.0 * std::log2 (peakFreq (x, (size_t) (sr * 0.5), sr, f0) / f0);
                    worst = juce::jmax (worst, std::abs (c));
                    spreadMax = juce::jmax (spreadMax, std::abs (c));
                }
                return worst;
            };
            const double off = measure (0), on = measure (100);
            check (off <= 0.1, "Stereo D1: Voice Variation 0% leaves every voice exactly in tune (worst " + juce::String (off, 3) + " cents)");
            check (on >= 1.0 && on <= 8.2, "Stereo D2: Voice Variation 100% detunes voices by up to 8 cents (largest " + juce::String (on, 2) + " cents)");
        }

        // E. Projects saved before these controls existed load mono, identical voices, reset phase.
        {
            auto q = makeSynth (sr);
            juce::MemoryBlock blob;
            q->getStateInformation (blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            xml->setAttribute (StateIDs::formatVersion, 2);
            for (auto* id : { ParamIDs::spread, ParamIDs::variation, ParamIDs::startPhase })
                if (auto* child = xml->getChildByAttribute ("id", id))
                    xml->removeChildElement (child, true);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            auto fresh = makeSynth (sr);
            setParam (*fresh, ParamIDs::spread, 80); setParam (*fresh, ParamIDs::variation, 80);
            fresh->setStateInformation (old.getData(), (int) old.getSize());
            auto is = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
            const bool ok = is (paramValue (*fresh, ParamIDs::spread), 0) && is (paramValue (*fresh, ParamIDs::variation), 0)
                            && is (paramValue (*fresh, ParamIDs::startPhase), 0);
            check (ok, "Stereo E: an older project loads with Spread 0%, Variation 0%, Start Phase Reset, so it sounds as it did");
            auto now = makeSynth (sr);
            check (is (paramValue (*now, ParamIDs::spread), 50) && is (paramValue (*now, ParamIDs::variation), 25)
                       && is (paramValue (*now, ParamIDs::startPhase), 1),
                   "Stereo E2: a new patch starts with Spread 50%, Variation 25%, Start Phase Random");
        }
    }
}
