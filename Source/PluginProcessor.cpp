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
    setChainOrder (Chain::defaultOrder());

    for (int t = 0; t < Mod::numTargets; ++t)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (parameters.getParameter (Mod::targetIds()[(size_t) t]));
        jassert (param != nullptr);
        modContext.ranges[(size_t) t] = param->getNormalisableRange();
        targetParams[(size_t) t] = parameters.getRawParameterValue (Mod::targetIds()[(size_t) t]);
    }
    voiceSettings.mod = &modContext;

    if (wrapperType == wrapperType_Standalone)
        outputFollower = std::make_unique<OutputFollower>();
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

    // Volume and the limiter run in short chunks so Volume can follow its modulation.
    const bool limiterOn = p (ParamIDs::limiter)->load() > 0.5f;
    for (int start = 0; start < numSamples; start += globalModInterval)
    {
        const int len = juce::jmin (globalModInterval, numSamples - start);

        const float volDb = globalVolumeDb (start);

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
        for (size_t t = 0; t < tapBuffers.size(); ++t)
            f.v[t] = tapBuffers[t][(size_t) i];
        f.v[LiveInfo::numStages - 1] = out != nullptr ? out[i] : 0.0f;
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
    }

    for (size_t i = 0; i < (size_t) Chain::numBlocks; ++i)
        v.order[i] = chainOrder[i].load();

    synth.setVoiceLimit ((int) p (ParamIDs::voices)->load());
}

// Volume, with its modulation, at sample `pos` of this block.
float SNSynthProcessor::globalVolumeDb (int pos)
{
    auto& m = modContext;
    float offset = 0.0f;
    if (m.numRoutes > 0)
    {
        float src[Mod::numSources];
        for (int s = 0; s < Mod::numSources; ++s)
            src[s] = m.newestSources[(size_t) s];
        src[Mod::lfo1] = lfoBuffers[0][(size_t) pos];
        src[Mod::lfo2] = lfoBuffers[1][(size_t) pos];
        src[Mod::modWheel] = m.modWheel;

        for (int r = 0; r < m.numRoutes; ++r)
            if (m.routes[(size_t) r].target == Mod::volume)
                offset += src[m.routes[(size_t) r].source] * m.routes[(size_t) r].amount;
    }
    return std::abs (offset) > 1.0e-7f ? Mod::applyOffset (m, Mod::volume, offset) : m.baseReal[(size_t) Mod::volume];
}

std::array<int, Chain::numBlocks> SNSynthProcessor::getChainOrder() const
{
    std::array<int, Chain::numBlocks> o {};
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = chainOrder[i].load();
    return o;
}

void SNSynthProcessor::setChainOrder (std::array<int, Chain::numBlocks> order)
{
    // Must contain each block once; otherwise fall back to the default.
    auto sorted = order;
    std::sort (sorted.begin(), sorted.end());
    if (sorted != Chain::defaultOrder())
        order = Chain::defaultOrder();

    for (size_t i = 0; i < order.size(); ++i)
        chainOrder[i].store (order[i]);

    juce::StringArray parts;
    for (int b : order)
        parts.add (juce::String (b));
    parameters.state.setProperty (StateIDs::chainOrder, parts.joinIntoString (","), nullptr);

    // Older versions only read the filter order; keep it in step.
    const auto f = getSlotOrder();
    parameters.state.setProperty (StateIDs::slotOrder,
                                  juce::String (f[0]) + "," + juce::String (f[1]) + "," + juce::String (f[2]), nullptr);
}

void SNSynthProcessor::moveBlock (int position, int newPosition)
{
    auto order = getChainOrder();
    const int n = (int) order.size();
    if (position < 0 || position >= n || newPosition < 0 || newPosition >= n || position == newPosition)
        return;
    const int block = order[(size_t) position];
    std::vector<int> v (order.begin(), order.end());
    v.erase (v.begin() + position);
    v.insert (v.begin() + newPosition, block);
    std::copy (v.begin(), v.end(), order.begin());
    setChainOrder (order);
}

std::array<int, 3> SNSynthProcessor::getSlotOrder() const
{
    std::array<int, 3> f {};
    int n = 0;
    for (int b : getChainOrder())
        if (Chain::isFilter (b))
            f[(size_t) n++] = b;
    return f;
}

void SNSynthProcessor::setSlotOrder (std::array<int, 3> filters)
{
    // Put the filters in this order, in the places filters already hold.
    auto order = getChainOrder();
    int n = 0;
    for (auto& b : order)
        if (Chain::isFilter (b))
            b = filters[(size_t) n++];
    setChainOrder (order);
}

void SNSynthProcessor::moveSlot (int position, int delta)
{
    const int other = position + delta;
    if (position < 0 || position > 2 || other < 0 || other > 2)
        return;
    auto f = getSlotOrder();
    std::swap (f[(size_t) position], f[(size_t) other]);
    setSlotOrder (f);
}

void SNSynthProcessor::applySlotOrderFromState()
{
    auto parse = [] (const juce::String& text, int count)
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");
        std::vector<int> v;
        if (parts.size() == count)
            for (auto& part : parts)
                v.push_back (part.getIntValue());
        return v;
    };

    auto chain = parse (parameters.state.getProperty (StateIDs::chainOrder).toString(), Chain::numBlocks);
    if (chain.size() == (size_t) Chain::numBlocks)
    {
        std::array<int, Chain::numBlocks> o {};
        std::copy (chain.begin(), chain.end(), o.begin());
        setChainOrder (o);
        return;
    }

    // Projects from before the chain could be reordered: filters in their
    // saved order, then Amp, then EQ (exactly how they used to run).
    auto f = parse (parameters.state.getProperty (StateIDs::slotOrder, "0,1,2").toString(), 3);
    std::array<int, Chain::numBlocks> o = Chain::defaultOrder();
    if (f.size() == 3)
        for (size_t i = 0; i < 3; ++i)
            o[i] = f[i];
    setChainOrder (o);
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
    // Their Rate and Smooth can be modulated, including by the LFOs
    // themselves: each update uses the LFOs' latest output (the sample
    // before), which keeps the loop stable.
    const int shapes[2] = { (int) p (ParamIDs::lfo1Shape)->load(), (int) p (ParamIDs::lfo2Shape)->load() };
    const int rateT[2]   = { Mod::lfo1Rate, Mod::lfo2Rate };
    const int smoothT[2] = { Mod::lfo1Smooth, Mod::lfo2Smooth };
    auto& m = modContext;

    for (auto& buf : lfoBuffers)
        if ((int) buf.size() < numSamples)
            buf.resize ((size_t) numSamples); // only if the host exceeds its announced block size

    bool anyLfoRoutes = false;
    for (int r = 0; r < m.numRoutes; ++r)
        anyLfoRoutes = anyLfoRoutes || m.routes[(size_t) r].target >= Mod::lfo1Rate;

    float rate[2] {}, coef[2] {};
    for (int i = 0; i < numSamples; ++i)
    {
        if (i % 16 == 0)
        {
            float offset[Mod::numTargets - Mod::lfo1Rate] {};
            if (anyLfoRoutes)
            {
                float src[Mod::numSources];
                for (int s = 0; s < Mod::numSources; ++s)
                    src[s] = m.newestSources[(size_t) s];
                src[Mod::lfo1] = lfoSmoothed[0];
                src[Mod::lfo2] = lfoSmoothed[1];
                src[Mod::modWheel] = m.modWheel;
                for (int r = 0; r < m.numRoutes; ++r)
                {
                    const auto& route = m.routes[(size_t) r];
                    if (route.target >= Mod::lfo1Rate)
                        offset[route.target - Mod::lfo1Rate] += src[route.source] * route.amount;
                }
            }
            for (int l = 0; l < 2; ++l)
            {
                const float ro = offset[rateT[l] - Mod::lfo1Rate], so = offset[smoothT[l] - Mod::lfo1Rate];
                rate[l] = std::abs (ro) > 1.0e-7f ? Mod::applyOffset (m, rateT[l], ro) : m.baseReal[(size_t) rateT[l]];
                rate[l] = juce::jlimit (0.05f, 20.0f, rate[l]);
                const float smoothPct = std::abs (so) > 1.0e-7f ? Mod::applyOffset (m, smoothT[l], so) : m.baseReal[(size_t) smoothT[l]];

                // Smooth rounds off corners: a gentle low-pass whose time is up
                // to a quarter of one LFO cycle at 100%.
                const float smoothSeconds = smoothPct * 0.01f * 0.25f / rate[l];
                coef[l] = smoothSeconds > 1.0e-5f ? 1.0f - std::exp (-1.0f / (smoothSeconds * (float) currentSampleRate)) : 1.0f;
            }
        }

        for (int l = 0; l < 2; ++l)
        {
            auto& phase = lfoPhases[(size_t) l];
            const float raw = lfoShapeValue (shapes[l], phase, lfoRandom[(size_t) l]);
            lfoSmoothed[(size_t) l] += (raw - lfoSmoothed[(size_t) l]) * coef[l];
            lfoBuffers[(size_t) l][(size_t) i] = lfoSmoothed[(size_t) l];
            phase += rate[l] / currentSampleRate;
            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                lfoRandom[(size_t) l] = lfoRng.nextFloat() * 2.0f - 1.0f;
            }
        }
    }

    for (int l = 0; l < 2; ++l)
    {
        voiceSettings.lfo[l] = lfoBuffers[(size_t) l].data();
        modContext.lfo[l] = lfoBuffers[(size_t) l].data();
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
