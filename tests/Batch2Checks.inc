// Small sound fixes (sound review items 12, 14, 15, 16, 18): no clicks from
// Sustain or filter-type changes, self-oscillating 2-pole filters, calibrated
// envelopes with a Curve, smoother Comb, gliding Formant, OTA Keep Bass.
// Included by RenderTest.cpp.

namespace Batch2Checks
{
    using SoundChecks::Event;
    using SoundChecks::neutral;
    using SoundChecks::rms;
    using SoundChecks::renderEvents;

    // 4th-order Butterworth high-pass (two biquads), for finding clicks.
    static std::vector<double> highPass4 (const std::vector<float>& x, double sr, double hz)
    {
        std::vector<double> y (x.begin(), x.end());
        for (double q : { 0.5412, 1.3066 })
        {
            auto c = juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass (sr, hz, q);
            double z1 = 0, z2 = 0;
            for (auto& v : y)
            {
                const double in = v;
                const double out = (c[0] * in + z1) / c[3];
                z1 = (c[1] * in - c[4] * out) / c[3] + z2;
                z2 = (c[2] * in - c[5] * out) / c[3];
                v = out;
            }
        }
        return y;
    }

    static double goertzel (const std::vector<float>& x, size_t from, size_t len, double hz, double sr)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr, cw = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0, wsum = 0;
        for (size_t i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) len);
            const double s0 = x[from + i] * win + cw * s1 - s2;
            s2 = s1; s1 = s0; wsum += win;
        }
        return std::sqrt (s1 * s1 + s2 * s2 - cw * s1 * s2) * 2.0 / wsum;
    }

    static void run (double sr)
    {
        using juce::MidiMessage;
        const int sec = (int) sr;

        // A. Moving Sustain while the envelope decays never makes it jump
        //    (target: largest step between samples 0.01).
        for (auto [from, to] : { std::pair { 0.4f, 0.8f }, std::pair { 0.8f, 0.2f } })
        {
            Envelope e;
            e.setSampleRate (sr);
            e.setParameters (0.001f, 1.0f, from, 0.3f);
            e.noteOn();
            float last = 0.0f, worst = 0.0f;
            for (int i = 0; i < sec; ++i)
            {
                if (i == (int) (0.15 * sr)) e.setParameters (0.001f, 1.0f, to, 0.3f);
                const float v = e.next();
                if (i > (int) (0.01 * sr)) worst = juce::jmax (worst, std::abs (v - last));
                last = v;
            }
            check (worst <= 0.01f, "Batch 2 A: moving Sustain " + juce::String (juce::roundToInt (from * 100)) + "% to "
                                       + juce::String (juce::roundToInt (to * 100)) + "% mid-decay doesn't jump (largest step "
                                       + juce::String (worst, 4) + ", target 0.01)");
        }

        // B. Switching filter type while a note plays doesn't click
        //    (target: click energy above 6 kHz at least 40 dB below the signal).
        {
            auto q = neutral (sr);
            setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
            setParam (*q, ParamIDs::slot (0, ParamIDs::slotCutoff), 3000.0f);
            setParam (*q, ParamIDs::slot (0, ParamIDs::slotEnv), 0);
            setParam (*q, ParamIDs::keyTracking, 0);
            const int order[] = { FilterType::ota, FilterType::sallenKey, FilterType::steinerParker, FilterType::polivoks,
                                  FilterType::comb, FilterType::formant, FilterType::ota, FilterType::off, FilterType::sallenKey };
            const int block = 480, per = sec / 4;
            std::vector<float> out;
            juce::AudioBuffer<float> buf (2, block);
            int n = 0;
            for (int t = 0; t < (int) std::size (order); ++t)
            {
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), (float) order[t]);
                for (int k = 0; k < per / block; ++k, ++n)
                {
                    juce::MidiBuffer m;
                    if (n == 0) m.addEvent (MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
                    q->processBlock (buf, m);
                    out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
                }
            }
            const auto hf = highPass4 (out, sr, 6000.0);
            double sigPeak = 0, worst = 0;
            for (size_t i = (size_t) (0.05 * sr); i < out.size(); ++i) sigPeak = juce::jmax (sigPeak, (double) std::abs (out[i]));
            for (int t = 1; t < (int) std::size (order); ++t)
            {
                const size_t at = (size_t) (t * (per / block) * block);
                for (size_t i = at - 200; i < at + (size_t) (0.02 * sr); ++i) worst = juce::jmax (worst, std::abs (hf[i]));
            }
            const double db = 20.0 * std::log10 (worst / sigPeak + 1e-12);
            check (db <= -40.0, "Batch 2 B: switching filter type mid-note doesn't click (8 switches, worst click "
                                    + juce::String (db, 1) + " dB below the signal, target -40)");
        }

        // C. Sallen-Key, Steiner-Parker and Polivoks self-oscillate at full
        //    Resonance: a steady tone at the Cutoff (within 5 cents), the same
        //    level from 110 Hz to 5 kHz (within +/-3 dB) and at 1x/2x/4x (within 1 dB).
        for (int type : { (int) FilterType::sallenKey, (int) FilterType::steinerParker, (int) FilterType::polivoks })
        {
            auto ring = [&] (float hz, int os, double& cents)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), (float) type);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotCutoff), hz);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotResonance), 100);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotEnv), 0);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotLevelMatch), 0);
                setParam (*q, ParamIDs::keyTracking, 0);
                setParam (*q, ParamIDs::oversampling, (float) os);
                setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
                setParam (*q, ParamIDs::osc1Level, 1); // a whisper to start it ringing
                setParam (*q, ParamIDs::volume, 0);
                auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, 33, (juce::uint8) 100) } }, 2 * sec);
                const double f = StereoChecks::peakFreq (x, x.size() - 65536, sr, hz);
                cents = 1200.0 * std::log2 (f / hz);
                return 20.0 * std::log10 (rms (x, x.size() - (size_t) (sr / 2), x.size()) + 1e-12);
            };
            double lo = 1e9, hi = -1e9, worstCents = 0;
            juce::String list;
            for (float hz : { 110.0f, 220.0f, 440.0f, 880.0f, 1760.0f, 3520.0f, 5000.0f })
            {
                double c = 0;
                const double db = ring (hz, 1, c);
                lo = juce::jmin (lo, db); hi = juce::jmax (hi, db); worstCents = juce::jmax (worstCents, std::abs (c));
                list << juce::String (db, 1) << " ";
            }
            double osLo = 1e9, osHi = -1e9, c = 0;
            for (int os : { 0, 1, 2 }) { const double db = ring (880.0f, os, c); osLo = juce::jmin (osLo, db); osHi = juce::jmax (osHi, db); }
            const auto name = Params::filterTypeNames()[type];
            check (lo > -40.0 && hi - lo <= 6.0, "Batch 2 C: " + name + " self-oscillates at a steady level 110 Hz-5 kHz (spread "
                                                    + juce::String (hi - lo, 2) + " dB, target 6; dBFS: " + list.trim() + ")");
            check (worstCents <= 5.0, "Batch 2 C: " + name + " self-oscillation is in tune with Cutoff (worst " + juce::String (worstCents, 2) + " cents, target 5)");
            check (osHi - osLo <= 1.0, "Batch 2 C: " + name + " self-oscillation level is the same at 1x/2x/4x ("
                                           + juce::String (osHi - osLo, 2) + " dB, target 1)");
        }

        // D. Decay and Release knob time = time to fall 99% (-40 dB), within 5%, at every Curve.
        for (float curve : { -1.0f, 0.0f, 1.0f })
        {
            double worst = 0;
            for (float t : { 0.05f, 0.5f, 3.0f })
            {
                Envelope e;
                e.setSampleRate (sr);
                e.setParameters (0.0005f, t, 0.0f, t, curve);
                e.noteOn();
                int i = 0, peakAt = -1, decayAt = -1;
                for (; i < (int) (sr * 10); ++i)
                {
                    const float v = e.next();
                    if (peakAt < 0 && v >= 1.0f) peakAt = i;
                    if (peakAt >= 0 && v <= 0.01f) { decayAt = i; break; }
                }
                const double dErr = std::abs ((decayAt - peakAt) / sr / t - 1.0);
                Envelope r;
                r.setSampleRate (sr);
                r.setParameters (0.0005f, 1.0f, 1.0f, t, curve);
                r.noteOn();
                for (int k = 0; k < (int) (0.05 * sr); ++k) r.next();
                r.noteOff();
                int rel = 0;
                while (r.next() > 0.01f && rel < (int) (sr * 10)) ++rel;
                const double rErr = std::abs ((rel + 1) / sr / t - 1.0);
                worst = juce::jmax (worst, juce::jmax (dErr, rErr));
            }
            check (worst <= 0.05, "Batch 2 D: Decay and Release times match the knob (time to -40 dB), Curve "
                                      + juce::String (juce::roundToInt (curve * 100)) + "% (worst " + juce::String (worst * 100.0, 2) + "% off, target 5%)");
        }

        // E. Comb: high harmonics ring for the same time on neighbouring notes
        //    (T60 of harmonic 8 at 440 vs 446 Hz within 5%).
        {
            auto t60 = [&] (float hz)
            {
                FilterSlot f;
                f.prepare ((float) sr);
                FilterSlotSettings s;
                s.type = FilterType::comb; s.resonance = 0.9f; s.oversample = 2; s.levelMatch = false;
                std::vector<float> y ((size_t) sec);
                for (size_t i = 0; i < y.size(); ++i) y[i] = f.process (i == 0 ? 0.5f : 0.0f, s, hz);
                const size_t win = 4096;
                const double a = goertzel (y, (size_t) (0.05 * sr), win, hz * 8.0, sr);
                const double b = goertzel (y, (size_t) (0.30 * sr), win, hz * 8.0, sr);
                const double dbPerSec = 20.0 * std::log10 (a / b) / 0.25;
                return 60.0 / dbPerSec;
            };
            const double a = t60 (440.0f), b = t60 (446.0f);
            check (std::abs (a / b - 1.0) <= 0.05, "Batch 2 E: Comb's 8th harmonic rings as long at 440 Hz as at 446 Hz (T60 "
                                                       + juce::String (a, 3) + " s vs " + juce::String (b, 3) + " s, target within 5%)");
        }

        // F. Formant: turning the Vowel knob glides, with no jumps in level (target 1 dB per 1% step,
        //    on a 55 Hz saw, whose close harmonics show the overall level rather than single peaks).
        {
            double worst = 0, prev = 0;
            for (int c = 0; c <= 100; ++c)
            {
                FilterSlot f;
                f.prepare ((float) sr);
                FilterSlotSettings s;
                s.type = FilterType::formant; s.resonance = 0.4f; s.character = (float) c * 0.01f; s.levelMatch = false;
                std::vector<float> y ((size_t) (sec / 4));
                double ph = 0;
                for (size_t i = 0; i < y.size(); ++i) { y[i] = f.process (0.5f * (float) (2.0 * ph - 1.0), s, 1000.0f); ph += 55.0 / sr; ph -= std::floor (ph); }
                const double db = 20.0 * std::log10 (rms (y, y.size() / 2, y.size()));
                if (c > 0) worst = juce::jmax (worst, std::abs (db - prev));
                prev = db;
            }
            check (worst <= 1.0, "Batch 2 F: Formant vowels glide without jumps (largest step " + juce::String (worst, 2) + " dB per 1%, target 1)");
        }

        // G. OTA Keep Bass: 50 Hz keeps its level as Resonance goes 0 to 90% (target within 1 dB).
        for (bool keep : { false, true })
        {
            auto level = [&] (float res)
            {
                FilterSlot f;
                f.prepare ((float) sr);
                FilterSlotSettings s;
                s.type = FilterType::ota; s.resonance = res; s.keepBass = keep; s.levelMatch = false;
                std::vector<float> y ((size_t) sec);
                for (size_t i = 0; i < y.size(); ++i) y[i] = f.process (0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * (double) i / sr), s, 1000.0f);
                return 20.0 * std::log10 (rms (y, y.size() / 2, y.size()));
            };
            const double change = level (0.9f) - level (0.0f);
            if (keep)
                check (std::abs (change) <= 1.0, "Batch 2 G: OTA Keep Bass on, bass level stays put as Resonance goes 0 to 90% ("
                                                     + juce::String (change, 2) + " dB, target within 1)");
            else
                check (change < -3.0, "Batch 2 G: OTA Keep Bass off keeps the classic thinning (" + juce::String (change, 2) + " dB at 90%)");
        }

        // H. Older projects: envelope times and Formant vowels are converted so they sound the same.
        {
            auto q = makeSynth (sr);
            setParam (*q, ParamIDs::aenvDecay, 1.0f);
            setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::formant);
            setParam (*q, ParamIDs::slot (0, ParamIDs::slotCharacter), 30.0f); // was "E" (20-40%)
            juce::MemoryBlock blob;
            q->getStateInformation (blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            xml->setAttribute (StateIDs::formatVersion, 3);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            auto fresh = makeSynth (sr);
            fresh->setStateInformation (old.getData(), (int) old.getSize());
            const float d = paramValue (*fresh, ParamIDs::aenvDecay), v = paramValue (*fresh, ParamIDs::slot (0, ParamIDs::slotCharacter));
            check (std::abs (d - 1.0f / (float) Envelope::stretch (Envelope::classicRatio)) < 0.01f && std::abs (v - 25.0f) < 0.2f,
                   "Batch 2 H: an older project's Decay 1 s loads as " + juce::String (d, 3) + " s (same sound), and its Formant E stays E ("
                       + juce::String (v, 1) + "%)");
        }
    }
}
