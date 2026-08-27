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

    captureRing.prepare (48000.0, 2);   // real rate arrives in prepareToPlay
    coordinator = std::make_unique<AnalysisCoordinator> (captureRing, displayModel);
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

void KeyGloProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    outputGain.reset (sampleRate, 0.03);
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputGainParam->load()));
    peakHold[0] = peakHold[1] = 0.0f;

    if (std::abs (captureRing.sampleRate() - sampleRate) > 0.5)
        captureRing.prepare (sampleRate, getTotalNumInputChannels());
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
