#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

SNSynthProcessor::SNSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SNSynthState", Params::createLayout())
{
    volumeParam = parameters.getRawParameterValue (ParamIDs::volume);

    for (int i = 0; i < 3; ++i)
    {
        auto get = [&] (const char* c) { return parameters.getRawParameterValue (ParamIDs::slot (i, c)); };
        slotParams[(size_t) i] = { get (ParamIDs::slotType), get (ParamIDs::slotCutoff), get (ParamIDs::slotResonance),
                                   get (ParamIDs::slotDrive), get (ParamIDs::slotEnv), get (ParamIDs::slotCharacter) };
    }
    parameters.state.setProperty (StateIDs::slotOrder, "0,1,2", nullptr);

    for (int t = 0; t < Mod::numTargets; ++t)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (parameters.getParameter (Mod::targetIds()[(size_t) t]));
        jassert (param != nullptr);
        modContext.ranges[(size_t) t] = param->getNormalisableRange();
        targetParams[(size_t) t] = parameters.getRawParameterValue (Mod::targetIds()[(size_t) t]);
    }
    voiceSettings.mod = &modContext;
}

float SNSynthProcessor::volumeDbToGain (float db)
{
    // Bottom of the range means silence; a tiny floor keeps the
    // multiplicative smoother valid.
    return db <= -60.0f ? 1.0e-5f : juce::Decibels::decibelsToGain (db);
}

void SNSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (auto& b : lfoBuffers)
        b.assign ((size_t) juce::jmax (samplesPerBlock, 512), 0.0f);
    limiterRelease = std::exp (-1.0f / (0.15f * (float) sampleRate));
    limiterEnv = 0.0f;
    voiceComp.reset (sampleRate, 0.05);
    voiceComp.setCurrentAndTargetValue (1.0f);
    for (auto& t : tapBuffers)
        t.assign ((size_t) juce::jmax (samplesPerBlock, 512), 0.0f);
    voiceSettings.live = &live;
    voiceSettings.lastNote = -1.0;
    updateVoiceSettings();
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (samplesPerBlock, 1), 1 };
    for (auto* bank : { &eqLow, &eqMid, &eqHigh })
        for (auto& f : *bank) { f.prepare (spec); f.reset(); }
    lastEq[0] = lastEq[1] = lastEq[2] = 1000.0f; // force coefficient update
    updateVoiceSettings();
    const float flat[3] = { modContext.baseReal[Mod::eqLow], modContext.baseReal[Mod::eqMid], modContext.baseReal[Mod::eqHigh] };
    updateEq (flat);

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (volumeDbToGain (volumeParam->load()));
}

bool SNSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SNSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Merges notes from the on-screen / computer keyboard into the incoming MIDI.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    const int n = buffer.getNumSamples();
    updateVoiceSettings();
    renderLfo (n);

    for (size_t t = 0; t < tapBuffers.size(); ++t)
    {
        if ((int) tapBuffers[t].size() < n)
            tapBuffers[t].resize ((size_t) n); // only if the host exceeds its announced block size
        std::fill (tapBuffers[t].begin(), tapBuffers[t].begin() + n, 0.0f);
        if (t < 4)
            voiceSettings.taps[t] = tapBuffers[t].data();
    }

    // Mod wheel (MIDI CC 1)
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isController() && msg.getControllerNumber() == 1)
            modContext.modWheel = (float) msg.getControllerValue() / 127.0f;
    }

    synth.renderNextBlock (buffer, midi, 0, n);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Voice Gain Compensation: turns the sum down as more notes play
    // (1 / square root of the number of notes), so chords don't jump in level.
    {
        const bool comp = p (ParamIDs::voiceGainComp)->load() > 0.5f;
        const int playing = synth.countActiveVoices();
        voiceComp.setTargetValue (comp ? 1.0f / std::sqrt ((float) juce::jmax (1, playing)) : 1.0f);
    }

    // EQ, Volume and the limiter run in short chunks so EQ and Volume can
    // follow their modulation.
    const bool limiterOn = p (ParamIDs::limiter)->load() > 0.5f;
    for (int start = 0; start < numSamples; start += globalModInterval)
    {
        const int len = juce::jmin (globalModInterval, numSamples - start);

        float eqDb[3], volDb;
        globalModValues (start, eqDb, volDb);

        updateEq (eqDb);
        for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
        {
            auto* d = buffer.getWritePointer (ch, start);
            for (int i = 0; i < len; ++i)
                d[i] = eqHigh[(size_t) ch].processSample (eqMid[(size_t) ch].processSample (eqLow[(size_t) ch].processSample (d[i])));
        }
        if (numChannels > 0)
            std::copy (buffer.getReadPointer (0, start), buffer.getReadPointer (0, start) + len, tapBuffers[4].begin() + start);

        outputGain.setTargetValue (volumeDbToGain (volDb));
        for (int i = start; i < start + len; ++i)
        {
            const float g = outputGain.getNextValue() * voiceComp.getNextValue();
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& x = buffer.getWritePointer (ch)[i];
                x *= g;
                if (! std::isfinite (x))
                    x = 0.0f; // safety: a broken value never reaches the speakers
                peak = juce::jmax (peak, std::abs (x));
            }
            if (peak > 1.0f)
                live.outClipped.store (true); // would have gone over 0 dB

            // Output Limiter: holds the output just under 0 dB. It reacts
            // instantly to peaks and lets go over about 150 ms.
            if (limiterOn)
            {
                limiterEnv = juce::jmax (peak, limiterEnv * limiterRelease);
                if (limiterEnv > limiterCeiling)
                {
                    const float lg = limiterCeiling / limiterEnv;
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.getWritePointer (ch)[i] *= lg;
                    live.limiting.store (true);
                }
            }
            else
                limiterEnv = 0.0f;

            // Safety ceiling, always on even with the Limiter off: nothing
            // leaves the synth above -0.3 dB. With the Limiter off, loud
            // peaks are clipped flat here (that's what the CLIP light warns about).
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& x = buffer.getWritePointer (ch)[i];
                x = juce::jlimit (-limiterCeiling, limiterCeiling, x);
            }
        }
    }

    // Output meter: peak (held by the screen) and a ~300 ms average.
    {
        float peak = 0.0f, sumSq = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* d = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                peak = juce::jmax (peak, std::abs (d[i]));
                if (ch == 0) sumSq += d[i] * d[i];
            }
        }
        if (numSamples > 0)
        {
            const float blockSeconds = (float) (numSamples / currentSampleRate);
            const float a = 1.0f - std::exp (-blockSeconds / 0.3f);
            meterMeanSquare += (sumSq / (float) numSamples - meterMeanSquare) * a;
        }
        live.outRms.store (std::sqrt (meterMeanSquare));
        float prev = live.outPeak.load();
        while (peak > prev && ! live.outPeak.compare_exchange_weak (prev, peak)) {}
        live.activeVoices.store (synth.countActiveVoices());
    }

    // Feed the displays.
    int w = live.writePos.load (std::memory_order_relaxed);
    const float* out = numChannels > 0 ? buffer.getReadPointer (0) : nullptr;
    for (int i = 0; i < numSamples; ++i)
    {
        auto& f = live.ring[(size_t) (w & (LiveInfo::ringSize - 1))];
        for (size_t t = 0; t < 5; ++t)
            f.v[t] = tapBuffers[t][(size_t) i];
        f.v[5] = out != nullptr ? out[i] : 0.0f;
        ++w;
    }
    live.writePos.store (w & 0x3fffffff, std::memory_order_release);
    live.lfoPhase.store ((float) lfoPhases[0]);
    live.lfo2Phase.store ((float) lfoPhases[1]);
}

void SNSynthProcessor::updateVoiceSettings()
{
    auto& v = voiceSettings;
    auto& m = modContext;

    // Every modulatable knob: plain value into the settings, and its
    // position for the modulation.
    for (int t = 0; t < Mod::numTargets; ++t)
    {
        const float real = targetParams[(size_t) t]->load();
        m.baseReal[(size_t) t] = real;
        m.baseNorm[(size_t) t] = m.ranges[(size_t) t].convertTo0to1 (real);
        applyTarget (v, t, real);
    }

    // Active routings
    m.numRoutes = 0;
    m.anyVoiceRoutes = false;
    for (int src = 0; src < Mod::numSources; ++src)
        for (int t = 0; t < Mod::numTargets; ++t)
        {
            const float a = modMatrix.get (src, t);
            if (std::abs (a) > 1.0e-5f)
            {
                m.routes[(size_t) m.numRoutes++] = { src, t, a };
                if (! Mod::isGlobal (t))
                    m.anyVoiceRoutes = true;
            }
        }

    v.lfoToCutoff    = p (ParamIDs::lfoCutoff)->load() * 0.01f;
    v.velocityAmount = p (ParamIDs::velocityAmount)->load() * 0.01f;
    v.keyTracking    = p (ParamIDs::keyTracking)->load() * 0.01f;
    v.bendRange      = p (ParamIDs::bendRange)->load();
    const int os     = 1 << juce::jlimit (0, 2, (int) p (ParamIDs::oversampling)->load());

    for (size_t i = 0; i < 3; ++i)
    {
        auto& slot = v.slots[i];
        slot.type       = (int) slotParams[i].type->load();
        slot.levelMatch = parameters.getRawParameterValue (ParamIDs::slot ((int) i, ParamIDs::slotLevelMatch))->load() > 0.5f;
        slot.oversample = os;
        v.order[i] = slotOrder[i].load();
    }

    synth.setVoiceLimit ((int) p (ParamIDs::voices)->load());
}

// EQ and Volume, with their modulation, at sample `pos` of this block.
void SNSynthProcessor::globalModValues (int pos, float* eqDb, float& volDb)
{
    auto& m = modContext;
    float offset[4] {};
    if (m.numRoutes > 0)
    {
        float src[Mod::numSources];
        for (int s = 0; s < Mod::numSources; ++s)
            src[s] = m.newestSources[(size_t) s];
        src[Mod::lfo1] = lfoBuffers[0][(size_t) pos];
        src[Mod::lfo2] = lfoBuffers[1][(size_t) pos];
        src[Mod::modWheel] = m.modWheel;

        for (int r = 0; r < m.numRoutes; ++r)
        {
            const auto& route = m.routes[(size_t) r];
            if (Mod::isGlobal (route.target))
                offset[route.target - Mod::eqLow] += src[route.source] * route.amount;
        }
    }
    for (int b = 0; b < 3; ++b)
        eqDb[b] = std::abs (offset[b]) > 1.0e-7f ? Mod::applyOffset (m, Mod::eqLow + b, offset[b]) : m.baseReal[(size_t) (Mod::eqLow + b)];
    volDb = std::abs (offset[3]) > 1.0e-7f ? Mod::applyOffset (m, Mod::volume, offset[3]) : m.baseReal[(size_t) Mod::volume];
}

void SNSynthProcessor::updateEq (const float* gains)
{
    using C = juce::dsp::IIR::ArrayCoefficients<float>;
    const auto sr = currentSampleRate;

    // Only recalculate a band when its gain has moved (no memory is
    // allocated here, so this is safe to do while modulating).
    if (std::abs (gains[0] - lastEq[0]) > 0.01f)
    {
        const auto c = C::makeLowShelf (sr, 100.0, 0.707f, juce::Decibels::decibelsToGain (gains[0]));
        for (auto& f : eqLow) *f.coefficients = c;
        lastEq[0] = gains[0];
    }
    if (std::abs (gains[1] - lastEq[1]) > 0.01f)
    {
        const auto c = C::makePeakFilter (sr, 1000.0, 0.7f, juce::Decibels::decibelsToGain (gains[1]));
        for (auto& f : eqMid) *f.coefficients = c;
        lastEq[1] = gains[1];
    }
    if (std::abs (gains[2] - lastEq[2]) > 0.01f)
    {
        const auto c = C::makeHighShelf (sr, 8000.0, 0.707f, juce::Decibels::decibelsToGain (gains[2]));
        for (auto& f : eqHigh) *f.coefficients = c;
        lastEq[2] = gains[2];
    }
}

std::array<int, 3> SNSynthProcessor::getSlotOrder() const
{
    return { slotOrder[0].load(), slotOrder[1].load(), slotOrder[2].load() };
}

void SNSynthProcessor::setSlotOrder (std::array<int, 3> order)
{
    // Must be a permutation of 0, 1, 2; otherwise fall back to the default.
    auto sorted = order;
    std::sort (sorted.begin(), sorted.end());
    if (sorted != std::array<int, 3> { 0, 1, 2 })
        order = { 0, 1, 2 };

    for (size_t i = 0; i < 3; ++i)
        slotOrder[i].store (order[i]);

    parameters.state.setProperty (StateIDs::slotOrder,
                                  juce::String (order[0]) + "," + juce::String (order[1]) + "," + juce::String (order[2]),
                                  nullptr);
}

void SNSynthProcessor::moveSlot (int position, int delta)
{
    const int other = position + delta;
    if (position < 0 || position > 2 || other < 0 || other > 2)
        return;

    auto order = getSlotOrder();
    std::swap (order[(size_t) position], order[(size_t) other]);
    setSlotOrder (order);
}

void SNSynthProcessor::applySlotOrderFromState()
{
    juce::StringArray parts;
    parts.addTokens (parameters.state.getProperty (StateIDs::slotOrder, "0,1,2").toString(), ",", "");
    std::array<int, 3> order { 0, 1, 2 };
    if (parts.size() == 3)
        for (size_t i = 0; i < 3; ++i)
            order[i] = parts[(int) i].getIntValue();
    setSlotOrder (order);
}


float SNSynthProcessor::lfoShapeValue (int shape, double phase, float randomValue)
{
    constexpr double twoPi = 6.283185307179586;
    switch (shape)
    {
        case 0:  return (float) std::sin (twoPi * phase);                                    // Sine
        case 1:  return (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);       // Triangle
        case 2:  return (float) (2.0 * phase - 1.0);                                         // Saw Up
        case 3:  return (float) (1.0 - 2.0 * phase);                                         // Saw Down
        case 4:  return phase < 0.5 ? 1.0f : -1.0f;                                          // Square
        default: return randomValue;                                                         // Random (a new level each cycle)
    }
}

void SNSynthProcessor::renderLfo (int numSamples)
{
    // Two free-running LFOs, so they keep their place between notes.
    const char* rateIds[2]   = { ParamIDs::lfoRate, ParamIDs::lfo2Rate };
    const char* shapeIds[2]  = { ParamIDs::lfo1Shape, ParamIDs::lfo2Shape };
    const char* smoothIds[2] = { ParamIDs::lfo1Smooth, ParamIDs::lfo2Smooth };

    for (int l = 0; l < 2; ++l)
    {
        auto& buf = lfoBuffers[(size_t) l];
        if ((int) buf.size() < numSamples)
            buf.resize ((size_t) numSamples); // only if the host exceeds its announced block size

        const float rate = p (rateIds[l])->load();
        const double inc = rate / currentSampleRate;
        const int shape = (int) p (shapeIds[l])->load();

        // Smooth rounds off corners: a gentle low-pass whose time is up to
        // a quarter of one LFO cycle at 100%.
        const float smooth = p (smoothIds[l])->load() * 0.01f;
        const float smoothSeconds = smooth * 0.25f / rate;
        const float coef = smoothSeconds > 1.0e-5f ? 1.0f - std::exp (-1.0f / (smoothSeconds * (float) currentSampleRate)) : 1.0f;

        auto& phase = lfoPhases[(size_t) l];
        for (int i = 0; i < numSamples; ++i)
        {
            const float raw = lfoShapeValue (shape, phase, lfoRandom[(size_t) l]);
            lfoSmoothed[(size_t) l] += (raw - lfoSmoothed[(size_t) l]) * coef;
            buf[(size_t) i] = lfoSmoothed[(size_t) l];
            phase += inc;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                lfoRandom[(size_t) l] = lfoRng.nextFloat() * 2.0f - 1.0f;
            }
        }
        voiceSettings.lfo[l] = buf.data();
        modContext.lfo[l] = buf.data();
    }
}

juce::AudioProcessorEditor* SNSynthProcessor::createEditor()
{
    return new SNSynthEditor (*this);
}

void SNSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.removeChild (state.getChildWithName ("MODULATION"), nullptr);
    state.appendChild (modMatrix.toValueTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SNSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            applySlotOrderFromState();
            modMatrix.fromValueTree (parameters.state.getChildWithName ("MODULATION"));

            // Projects from before modulation had one "LFO to Cutoff" knob for
            // all filters. Turn it into LFO 1 routings on each filter's Cutoff
            // (100% used to mean +/- 3 octaves; 100% routing = 10 octaves).
            auto* old = parameters.getParameter (ParamIDs::lfoCutoff);
            const float amount = p (ParamIDs::lfoCutoff)->load() * 0.01f;
            if (amount > 0.0f)
            {
                for (int s = 0; s < ParamIDs::numSlots; ++s)
                    modMatrix.set (Mod::lfo1, Mod::filterTarget (s, 0), amount * 3.0f / Mod::cutoffOctavesAtFull);
                old->setValueNotifyingHost (0.0f);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SNSynthProcessor();
}
