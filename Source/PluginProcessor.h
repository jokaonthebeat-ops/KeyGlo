/*
    KeyGloProcessor - milestone 1 (approved animated UI).

    The audio path is an honest pass-through with the smoothed output trim
    applied; peak meters are the only per-sample analysis so far. The
    analysis engine (beat key/scale/BPM, vocal range, hook fit, 808 tuning)
    arrives in milestones 2-4 per CLAUDE_MASTER_BUILD_PROMPT.md and will
    publish through the same AnalysisDisplayModel the UI already reads.

    Real-time rules (QA_ACCEPTANCE_CRITERIA.md): no allocations, locks, file
    I/O, image work, JSON or logging in processBlock().
*/

#pragma once
#include <JuceHeader.h>
#include "AnalysisModel.h"
#include "analysis/CaptureRing.h"
#include "analysis/AnalysisCoordinator.h"

namespace keyglo
{

// Parameter IDs - 08_LAYOUT/parameters.json, verbatim.
namespace pid
{
    inline constexpr const char* rangeSense         = "rangeSense";
    inline constexpr const char* keySense           = "keySense";
    inline constexpr const char* analysisSmooth     = "analysisSmooth";
    inline constexpr const char* previewMix         = "previewMix";
    inline constexpr const char* fineTuneCents      = "fineTuneCents";
    inline constexpr const char* outputGainDb       = "outputGainDb";
    inline constexpr const char* transposeSemitones = "transposeSemitones";
    inline constexpr const char* previewRecommended = "previewRecommended";
    inline constexpr const char* sampleSolo         = "sampleSolo";
    inline constexpr const char* pluginBypass       = "pluginBypass";
}

class KeyGloProcessor : public juce::AudioProcessor
{
public:
    KeyGloProcessor();
    ~KeyGloProcessor() override = default;

    // --- AudioProcessor ---------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- KeyGlo -----------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS()         { return apvts; }
    AnalysisDisplayModel& getDisplayModel()                { return displayModel; }

    // Analysis engine (milestone 2). All asynchronous; results publish
    // through the display model.
    void analyseFileAsync (const juce::File& f)            { coordinator->analyseFileAsync (f); }
    void analyseCaptureNow()                               { coordinator->analyseRingNow(); }
    bool isAnalysisBusy() const                            { return coordinator->isBusy(); }

    // Vocal engine (milestone 3).
    void startRangeTest()                                  { coordinator->startRangeTest(); }
    void advanceRangeTest()                                { coordinator->advanceRangeTest(); }
    void cancelRangeTest()                                 { coordinator->cancelRangeTest(); }
    bool saveArtistProfile (const juce::String& name)      { return coordinator->saveProfile (name); }
    bool loadArtistProfile (const juce::String& name)      { return coordinator->loadProfile (name); }
    const ArtistProfile& getArtistProfile() const          { return coordinator->vocals().getProfile(); }
    bool hasArtistProfile() const                          { return coordinator->vocals().hasProfile(); }
    VocalEngine& getVocalEngine()                          { return coordinator->vocals(); }

    float getPeakDb (int channel) const
    {
        return peakDb[juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    // Editor scale persisted with the session state (persistent_state:
    // "windowScale"), plus the two accessibility switches.
    float getSavedUIScale() const                          { return savedUIScale.load(); }
    void setSavedUIScale (float s)                         { savedUIScale.store (s); }
    bool getReduceMotion() const                           { return reduceMotion.load(); }
    void setReduceMotion (bool b)                          { reduceMotion.store (b); }
    bool getLowPowerMode() const                           { return lowPower.load(); }
    void setLowPowerMode (bool b)                          { lowPower.store (b); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    AnalysisDisplayModel displayModel;
    CaptureRing captureRing;
    std::unique_ptr<AnalysisCoordinator> coordinator;

    juce::SmoothedValue<float> outputGain { 1.0f };
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    std::atomic<float> peakDb[2] { -120.0f, -120.0f };
    float peakHold[2] { 0.0f, 0.0f };
    double sampleRateHz = 48000.0;

    std::atomic<float> savedUIScale { 1.0f };
    std::atomic<bool> reduceMotion { false }, lowPower { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyGloProcessor)
};

} // namespace keyglo
