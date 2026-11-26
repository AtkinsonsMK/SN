// Stress checks, reference recordings and the real-time check (engineering
// review Phase 3). Included by RenderTest.cpp; run with --stress.
#include <dlfcn.h>
#include <pthread.h>

// Records which locks the audio thread takes (it must not wait on other threads).
// Only the thread under test is counted, and a lock taken again (JUCE's synth
// lock is re-entered for each note) counts once.
static std::atomic<long> lockCount { 0 };
static thread_local bool countLocks = false;
static thread_local pthread_mutex_t* seenLocks[32] {};
static thread_local int numSeenLocks = 0;
extern "C" int pthread_mutex_lock (pthread_mutex_t* m)
{
    using Fn = int (*) (pthread_mutex_t*);
    static Fn real = (Fn) dlsym (RTLD_NEXT, "pthread_mutex_lock");
    if (countLocks)
    {
        bool seen = false;
        for (int i = 0; i < numSeenLocks; ++i) seen = seen || seenLocks[i] == m;
        if (! seen)
        {
            if (numSeenLocks < 32) seenLocks[numSeenLocks++] = m;
            lockCount.fetch_add (1, std::memory_order_relaxed);
        }
    }
    return real (m);
}

namespace StressChecks
{
    static constexpr float ceiling = 0.9661f; // -0.3 dB

    // Sets every control to a random position (as a damaged or extreme preset might).
    static void randomise (SNSynthProcessor& p, juce::Random& r)
    {
        for (auto* param : p.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            if (ranged == nullptr) continue;
            const auto id = ranged->getParameterID();
            if (id == ParamIDs::volume) continue; // keep the level sensible
            ranged->setValueNotifyingHost (r.nextFloat());
        }
        p.modMatrix.clear();
        for (int k = 0; k < 4; ++k)
            p.modMatrix.set (r.nextInt (Mod::numSources), r.nextInt (Mod::numTargets), r.nextFloat() * 2.0f - 1.0f);
    }

    struct Result { bool finite = true; float peak = 0.0f; long allocs = 0, locks = 0; int blocks = 0; };

    static Result play (SNSynthProcessor& p, double sr, int block, double seconds, juce::Random* r = nullptr)
    {
        Result res;
        juce::AudioBuffer<float> buf (2, block);
        const int total = (int) (sr * seconds);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int n : { 36, 48, 55, 60, 64, 67, 72, 84 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) (r != nullptr ? 1 + r->nextInt (127) : 100)), 0);
            if (pos >= total / 2 && pos - block < total / 2)
                for (int n : { 36, 55, 72 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            countAllocs = true; countLocks = true; numSeenLocks = 0;
            const long a0 = allocCount.load(), l0 = lockCount.load();
            p.processBlock (buf, midi);
            res.allocs += allocCount.load() - a0;
            res.locks += lockCount.load() - l0;
            countAllocs = false; countLocks = false;
            ++res.blocks;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    res.finite = res.finite && std::isfinite (v);
                    res.peak = juce::jmax (res.peak, std::abs (v));
                }
        }
        return res;
    }

    static std::unique_ptr<SNSynthProcessor> make (double sr, int block)
    {
        auto p = std::make_unique<SNSynthProcessor>();
        p->setPlayConfigDetails (0, 2, sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }

    // Known patches whose sound is saved in tests/reference. Any change to the
    // sound shows up as a difference (so it is either fixed or the reference updated on purpose).
    struct RefPatch { const char* name; std::function<void (SNSynthProcessor&)> set; };
    static std::vector<RefPatch> refPatches()
    {
        auto sp = [] (SNSynthProcessor& p, const juce::String& id, float v) { setParam (p, id, v); };
        return {
            { "default", [] (SNSynthProcessor&) {} },
            { "ota_res_drive", [sp] (SNSynthProcessor& p) { sp (p, ParamIDs::slot (0, ParamIDs::slotResonance), 80); sp (p, ParamIDs::slot (0, ParamIDs::slotDrive), 60); } },
            { "steiner_polivoks", [sp] (SNSynthProcessor& p) { sp (p, ParamIDs::slot (0, ParamIDs::slotType), FilterType::steinerParker);
                                                               sp (p, ParamIDs::slot (1, ParamIDs::slotType), FilterType::polivoks); sp (p, ParamIDs::slot (1, ParamIDs::slotResonance), 70); } },
            { "comb_formant", [sp] (SNSynthProcessor& p) { sp (p, ParamIDs::slot (0, ParamIDs::slotType), FilterType::comb);
                                                           sp (p, ParamIDs::slot (1, ParamIDs::slotType), FilterType::formant); } },
            { "fm_ring", [sp] (SNSynthProcessor& p) { sp (p, ParamIDs::fm2to1, 40); sp (p, ParamIDs::ringMod, 50); sp (p, ParamIDs::osc2Coarse, 7); } },
            { "lfo_mod_eq", [sp] (SNSynthProcessor& p) { p.modMatrix.set (Mod::lfo1, Mod::filterTarget (0, 0), 0.3f); sp (p, ParamIDs::lfoRate, 5);
                                                         sp (p, "eq_low", 6); sp (p, "eq_high", -6); } },
        };
    }

    static std::vector<float> renderRef (const RefPatch& rp)
    {
        auto p = make (48000.0, 512);
        setParam (*p, ParamIDs::drift, 0);
        rp.set (*p);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 24; ++b) // 0.256 s
        {
            juce::MidiBuffer midi;
            if (b == 0) for (int n : { 48, 55, 64 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            if (b == 16) midi.addEvent (juce::MidiMessage::noteOff (1, 55), 0);
            p->processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
        }
        return out;
    }

    static juce::File refDir()
    {
        return juce::File (SN_SOURCE_DIR).getChildFile ("tests/reference");
    }

    static void writeReferences()
    {
        refDir().createDirectory();
        for (auto& rp : refPatches())
        {
            auto x = renderRef (rp);
            refDir().getChildFile (juce::String (rp.name) + ".f32").replaceWithData (x.data(), x.size() * sizeof (float));
            std::cout << "wrote " << rp.name << std::endl;
        }
    }

    static void run()
    {
        // 1. Every sample rate and buffer size: no broken values, never over the ceiling,
        //    no memory allocated, and at most the 2 known uncontended locks per block
        //    (JUCE's synth lock and the on-screen keyboard's note queue).
        {
            int combos = 0, bad = 0;
            long allocs = 0, maxLocksPerBlock = 0;
            for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
                for (int block : { 1, 7, 64, 512, 4096 })
                {
                    auto p = make (sr, block);
                    play (*p, sr, block, 0.05); // warm up
                    auto r = play (*p, sr, block, 0.4);
                    ++combos;
                    if (! r.finite || r.peak > ceiling) ++bad;
                    allocs += r.allocs;
                    maxLocksPerBlock = juce::jmax (maxLocksPerBlock, (long) std::ceil ((double) r.locks / r.blocks));
                }
            check (bad == 0, "Stress: " + juce::String (combos) + " sample-rate / buffer-size combinations play cleanly (" + juce::String (bad) + " bad)");
            check (allocs == 0, "Real-time: no memory allocated at any rate or buffer size (" + juce::String ((juce::int64) allocs) + ")");
            check (maxLocksPerBlock <= 2, "Real-time: no new locks in the audio thread (" + juce::String ((juce::int64) maxLocksPerBlock) + " per block, 2 known)");
        }

        // 2. Random extreme patches across all filter types.
        {
            juce::Random r (20261126);
            int bad = 0; float worst = 0.0f; long allocs = 0;
            for (int k = 0; k < 360; ++k)
            {
                const double sr = (k % 3 == 0) ? 44100.0 : (k % 3 == 1 ? 48000.0 : 96000.0);
                auto p = make (sr, 512);
                randomise (*p, r);
                auto res = play (*p, sr, 512, 0.3, &r);
                if (! res.finite || res.peak > ceiling) ++bad;
                worst = juce::jmax (worst, res.peak);
                allocs += res.allocs;
            }
            check (bad == 0 && allocs == 0, "Stress: 360 random extreme patches stay finite and under -0.3 dBFS (worst "
                                                + juce::String (juce::Decibels::gainToDecibels (worst), 2) + " dBFS, "
                                                + juce::String ((juce::int64) allocs) + " allocations)");
        }

        // 3. Damaged save files: random bytes and cut-short real saves never crash, and it still plays.
        {
            juce::Random r (7);
            auto good = make (48000.0, 512);
            juce::MemoryBlock real;
            good->getStateInformation (real);
            int ok = 0;
            for (int k = 0; k < 200; ++k)
            {
                auto p = make (48000.0, 512);
                juce::MemoryBlock blob;
                if (k % 2 == 0)
                {
                    blob.setSize ((size_t) r.nextInt (4000));
                    for (size_t i = 0; i < blob.getSize(); ++i) blob[i] = (char) r.nextInt (256);
                }
                else
                {
                    blob = real;
                    blob.setSize ((size_t) r.nextInt ((int) real.getSize()));
                    for (int f = 0; f < 5 && blob.getSize() > 0; ++f) blob[(size_t) r.nextInt ((int) blob.getSize())] = (char) r.nextInt (256);
                }
                p->setStateInformation (blob.getData(), (int) blob.getSize());
                auto res = play (*p, 48000.0, 512, 0.1);
                if (res.finite && res.peak <= ceiling) ++ok;
            }
            check (ok == 200, "Stress: 200 damaged save files load without trouble (" + juce::String (ok) + " fine)");
        }

        // 4. Reference recordings: the sound of 6 known patches hasn't changed.
        for (auto& rp : refPatches())
        {
            auto file = refDir().getChildFile (juce::String (rp.name) + ".f32");
            juce::MemoryBlock saved;
            if (! file.loadFileAsData (saved))
            {
                check (false, juce::String ("Reference recording missing: ") + rp.name + " (run with --write-references)");
                continue;
            }
            auto x = renderRef (rp);
            const auto* ref = static_cast<const float*> (saved.getData());
            const size_t n = juce::jmin (x.size(), saved.getSize() / sizeof (float));
            double diff = 0.0, sig = 0.0;
            for (size_t i = 0; i < n; ++i) { diff += (x[i] - ref[i]) * (double) (x[i] - ref[i]); sig += (double) ref[i] * ref[i]; }
            const double db = 10.0 * std::log10 ((diff + 1.0e-30) / (sig + 1.0e-30));
            // On the machine that recorded them the match is exact (about -316 dB).
            // Other CPUs and maths libraries round slightly differently, which
            // reached -66.6 dB on GitHub's runners, so allow -60 dB (0.1% of the
            // signal): far below hearing, yet any real change to a sound is
            // tens of dB larger.
            check (n == x.size() && db < -60.0, juce::String ("Reference: ") + rp.name + " sounds as recorded (difference "
                                                   + juce::String (db, 1) + " dB)");
        }
    }

    // CPU benchmark: 16 notes, 3 filters at 4x, FM on, for 5 s of audio.
    static void bench()
    {
        const double sr = 48000.0;
        auto p = make (sr, 512);
        setParam (*p, ParamIDs::voices, 16);
        setParam (*p, ParamIDs::oversampling, 2);
        setParam (*p, ParamIDs::fm2to1, 30);
        for (int s = 0; s < 3; ++s) setParam (*p, ParamIDs::slot (s, ParamIDs::slotType), s == 0 ? FilterType::ota : (s == 1 ? FilterType::steinerParker : FilterType::sallenKey));
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer on;
        for (int n = 0; n < 16; ++n) on.addEvent (juce::MidiMessage::noteOn (1, 40 + n * 2, (juce::uint8) 100), 0);
        p->processBlock (buf, on);
        const int blocks = (int) (sr * 5.0 / 512);
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int b = 0; b < blocks; ++b) { juce::MidiBuffer none; p->processBlock (buf, none); }
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        const double load = 100.0 * secs / 5.0;
        std::cout << "BENCH  16 voices, 3 filters at 4x, FM on: " << juce::String (load, 1) << "% of one core" << std::endl;
        check (load < 150.0, "CPU benchmark runs (" + juce::String (load, 1) + "% of one core; fails only above 150%)");
    }
}
