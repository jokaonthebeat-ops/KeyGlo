#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace keyglo
{

KeyGloProcessor::KeyGloProcessor()
    : juce::AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KeyGloState", createLayout())
{
    outputGainParam = apvts.getRawParameterValue (pid::outputGainDb);
    bypassParam     = apvts.getRawParameterValue (pid::pluginBypass);
    previewMixParam = apvts.getRawParameterValue (pid::previewMix);
    fineTuneParam   = apvts.getRawParameterValue (pid::fineTuneCents);
    transposeParam  = apvts.getRawParameterValue (pid::transposeSemitones);
    abParam         = apvts.getRawParameterValue (pid::previewRecommended);
    soloParam       = apvts.getRawParameterValue (pid::sampleSolo);

    captureRing.prepare (48000.0, 2);   // real rate arrives in prepareToPlay
    coordinator = std::make_unique<AnalysisCoordinator> (captureRing, displayModel);
    coordinator->setPreviewPlayer (&previewPlayer);
}

juce::AudioProcessorValueTreeState::ParameterLayout KeyGloProcessor::createLayout()
{
    using P = juce::AudioProcessorValueTreeState;
    P::ParameterLayout layout;

    // 08_LAYOUT/parameters.json, ranges/defaults verbatim.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::rangeSense, 1 }, "Range Sense",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.72f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::keySense, 1 }, "Key Sense",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.85f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::analysisSmooth, 1 }, "Analysis Smooth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.6f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::previewMix, 1 }, "Preview Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.4f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::fineTuneCents, 1 }, "Fine Tune",
        juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 4.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int)
            {
                const int c = juce::roundToInt (v);
                return (c > 0 ? "+" : "") + juce::String (c) + " cents";
            })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::outputGainDb, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), -2.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 1) + " dB"; })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::transposeSemitones, 1 }, "Transpose Preview",
        juce::StringArray { "-4", "-3", "-2", "-1", "Original", "+1", "+2", "+3", "+4" }, 2));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::previewRecommended, 1 }, "A/B Recommended", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::sampleSolo, 1 }, "Sample Solo", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::pluginBypass, 1 }, "Bypass", false));

    return layout;
}

void KeyGloProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    outputGain.reset (sampleRate, 0.03);
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputGainParam->load()));
    peakHold[0] = peakHold[1] = 0.0f;

    if (std::abs (captureRing.sampleRate() - sampleRate) > 0.5)
        captureRing.prepare (sampleRate, getTotalNumInputChannels());

    shifter.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (shifter.latencySamples());
    soloBlend.reset (sampleRate, 0.03);
    soloBlend.setCurrentAndTargetValue (0.0f);
    soloScratch.setSize (juce::jmax (2, getTotalNumOutputChannels()),
                         juce::jmax (16, samplesPerBlock));
}

bool KeyGloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void KeyGloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const bool bypassed   = bypassParam->load() > 0.5f;

    // Capture the INPUT (pre-trim) for the analysis engine - the analyser
    // reads the source material, not our output level.
    captureRing.write (buffer);

    // Host transport for the "host BPM in live sessions" rule. getPosition()
    // is allocation-free; atomics carry it to the worker.
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            coordinator->setHostTempo (position->getBpm().orFallback (0.0),
                                       position->getIsPlaying());

    // --- transpose preview (milestone 4) -----------------------------------
    // The shifter always runs (its half-grain dry delay IS the reported
    // latency, so it must never drop out of the chain); what varies is the
    // wet amount: previewMix while B (recommended) is selected and a shift
    // is dialled in, zero on A or bypass. All moves are smoothed inside.
    {
        const int semis = juce::jlimit (0, 8, juce::roundToInt (transposeParam->load())) - 4;
        const float cents = fineTuneParam->load();
        // Armed only once a real key result exists: parameters.json ships
        // non-neutral defaults (B selected, -2 st, 40 % mix), and a fresh
        // analyzer instance must never pitch-shift the program on its own.
        const bool engaged = displayModel.previewArmed()
                              && abParam->load() > 0.5f
                              && (semis != 0 || std::abs (cents) > 0.5f)
                              && ! bypassed;
        const float ratio = std::pow (2.0f, ((float) semis + cents / 100.0f) / 12.0f);
        shifter.process (buffer, ratio, engaged ? previewMixParam->load() : 0.0f);
    }

    // --- 808 solo audition --------------------------------------------------
    // Cross-blends the program with the looping TUNED sample; the blend is
    // smoothed so toggling Solo never clicks.
    {
        const bool soloOn = soloParam->load() > 0.5f && previewPlayer.hasSample()
                             && ! bypassed;
        soloBlend.setTargetValue (soloOn ? 1.0f : 0.0f);

        if ((soloOn || soloBlend.getCurrentValue() > 0.001f)
             && soloScratch.getNumSamples() >= numSamples
             && soloScratch.getNumChannels() >= numChannels)
        {
            // A no-allocation view of the scratch at this block's length, so
            // the player's loop position advances by exactly one block.
            juce::AudioBuffer<float> view (soloScratch.getArrayOfWritePointers(),
                                           numChannels, numSamples);
            view.clear();
            if (previewPlayer.render (view, sampleRateHz))
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const float blend = soloBlend.getNextValue();
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.setSample (ch, i,
                            buffer.getSample (ch, i) * (1.0f - blend)
                              + soloScratch.getSample (ch, i) * blend);
                }
            }
            else
            {
                soloBlend.skip (numSamples);
            }
        }
        else
        {
            soloBlend.skip (numSamples);
        }
    }

    // Bypass still glides through the gain smoother to unity, so engaging or
    // releasing it cannot click (QA: "Bypass does not click").
    outputGain.setTargetValue (bypassed ? 1.0f
                                        : juce::Decibels::decibelsToGain (outputGainParam->load()));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            data[i] *= g;
        }
    }

    // Peak meters with block-rate release; atomics only, no locks.
    const float release = std::exp (-(float) numSamples / ((float) sampleRateHz * 0.35f));
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float mag = buffer.getMagnitude (ch, 0, numSamples);
        peakHold[ch] = juce::jmax (mag, peakHold[ch] * release);
        peakDb[ch].store (juce::Decibels::gainToDecibels (peakHold[ch], -120.0f),
                          std::memory_order_relaxed);
    }
    for (int ch = numChannels; ch < 2; ++ch)
        peakDb[ch].store (peakDb[0].load (std::memory_order_relaxed),
                          std::memory_order_relaxed);
}

void KeyGloProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("windowScale", savedUIScale.load(), nullptr);
    state.setProperty ("reduceMotion", reduceMotion.load(), nullptr);
    state.setProperty ("lowPower", lowPower.load(), nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void KeyGloProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid())
        return;

    savedUIScale.store ((float) (double) state.getProperty ("windowScale", 1.0));
    reduceMotion.store ((bool) state.getProperty ("reduceMotion", false));
    lowPower.store ((bool) state.getProperty ("lowPower", false));
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* KeyGloProcessor::createEditor()
{
    return new KeyGloEditor (*this);
}

} // namespace keyglo

// Standard JUCE factory hook.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new keyglo::KeyGloProcessor();
}
