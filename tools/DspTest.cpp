// -----------------------------------------------------------------------------
//  KeyGlo deterministic test suite - milestone 1 (UI + honest pass-through).
//
//    make dsptest
//
//  Ground-truth style checks (the VoxGlo lesson: a harness must be able to
//  fail): known input levels through known gain settings must land on known
//  output levels; state must round-trip; the editor must build headlessly
//  with every required component present and every asset loaded.
// -----------------------------------------------------------------------------

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>

using namespace keyglo;

static int checksRun = 0, checksFailed = 0;

static void check (bool condition, const juce::String& what)
{
    ++checksRun;
    if (! condition)
    {
        ++checksFailed;
        std::printf ("  FAIL  %s\n", what.toRawUTF8());
    }
}

static void checkNear (double actual, double expected, double tolerance,
                       const juce::String& what)
{
    check (std::abs (actual - expected) <= tolerance,
           what + "  (got " + juce::String (actual, 4)
                + ", expected " + juce::String (expected, 4)
                + " +/- " + juce::String (tolerance, 4) + ")");
}

// Feed a full-scale * amp sine and return the steady-state output peak in dB.
static float measureThroughput (KeyGloProcessor& p, float amp)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double phase = 0.0;
    float peak = 0.0f;

    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * 997.0 / 48000.0;
            const float v = amp * (float) std::sin (phase);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        p.processBlock (buffer, midi);
        if (block >= 30)   // past the 30 ms gain smoother
            peak = juce::jmax (peak, buffer.getMagnitude (0, 0, 512));
    }
    return juce::Decibels::gainToDecibels (peak, -120.0f);
}

static void setParam (KeyGloProcessor& p, const char* id, float plainValue)
{
    auto* param = p.getAPVTS().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("KeyGlo dsptest (%s)\n", JucePlugin_VersionString);

    // --- parameter roster & defaults (parameters.json) ---------------------
    {
        KeyGloProcessor p;
        struct Expect { const char* id; float def; };
        const Expect expects[] = {
            { pid::rangeSense, 0.72f }, { pid::keySense, 0.85f },
            { pid::analysisSmooth, 0.6f }, { pid::previewMix, 0.4f },
            { pid::fineTuneCents, 4.0f }, { pid::outputGainDb, -2.0f },
        };
        for (const auto& e : expects)
        {
            auto* raw = p.getAPVTS().getRawParameterValue (e.id);
            check (raw != nullptr, juce::String (e.id) + " exists");
            if (raw != nullptr)
                checkNear (raw->load(), e.def, 1.0e-4, juce::String (e.id) + " default");
        }

        auto* transpose = dynamic_cast<juce::AudioParameterChoice*> (
            p.getAPVTS().getParameter (pid::transposeSemitones));
        check (transpose != nullptr, "transposeSemitones is a choice parameter");
        if (transpose != nullptr)
        {
            check (transpose->choices.size() == 9, "transpose has 9 choices");
            check (transpose->getIndex() == 2, "transpose default is -2");
            check (transpose->choices[4] == "Original", "choice 4 is Original");
        }

        check (p.getAPVTS().getRawParameterValue (pid::previewRecommended)->load() > 0.5f,
               "previewRecommended defaults on");
        check (p.getAPVTS().getRawParameterValue (pid::sampleSolo)->load() < 0.5f,
               "sampleSolo defaults off");
        check (p.getAPVTS().getRawParameterValue (pid::pluginBypass)->load() < 0.5f,
               "bypass defaults off");
    }

    // --- gain path ---------------------------------------------------------
    {
        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        // Default output is -2 dB: -12 dBFS in must come out near -14 dBFS.
        const float defaultOut = measureThroughput (p, 0.251189f);   // -12 dBFS
        checkNear (defaultOut, -14.0, 0.15, "default -2 dB trim applied");

        setParam (p, pid::outputGainDb, 0.0f);
        const float unityOut = measureThroughput (p, 0.251189f);
        checkNear (unityOut, -12.0, 0.1, "0 dB trim is unity");

        setParam (p, pid::outputGainDb, 6.0f);
        const float boosted = measureThroughput (p, 0.251189f);
        checkNear (boosted, -6.0, 0.15, "+6 dB trim");

        // Bypass ignores the trim and passes unity.
        setParam (p, pid::outputGainDb, -24.0f);
        setParam (p, pid::pluginBypass, 1.0f);
        const float bypassed = measureThroughput (p, 0.251189f);
        checkNear (bypassed, -12.0, 0.1, "bypass passes unity regardless of trim");

        // Meters follow the signal.
        check (p.getPeakDb (0) > -13.0f && p.getPeakDb (0) < -10.0f,
               "peak meter tracks a -12 dBFS sine (got "
                 + juce::String (p.getPeakDb (0), 2) + " dB)");
    }

    // --- bypass engages without a click ------------------------------------
    {
        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        setParam (p, pid::outputGainDb, 12.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float maxStep = 0.0f;
        float previous = 0.0f;

        for (int block = 0; block < 30; ++block)
        {
            if (block == 15)
                setParam (p, pid::pluginBypass, 1.0f);   // flip mid-stream

            for (int i = 0; i < 512; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * 200.0 / 48000.0;
                buffer.setSample (0, i, 0.2f * (float) std::sin (phase));
                buffer.setSample (1, i, 0.2f * (float) std::sin (phase));
            }
            p.processBlock (buffer, midi);

            for (int i = 0; i < 512; ++i)
            {
                const float v = buffer.getSample (0, i);
                if (block > 0 || i > 0)
                    maxStep = juce::jmax (maxStep, std::abs (v - previous));
                previous = v;
            }
        }

        // A 200 Hz sine at these gains moves < 0.03 per sample; an unsmoothed
        // 12 dB drop would jump ~0.6 in one sample.
        check (maxStep < 0.08f,
               "bypass transition is smoothed (max sample step "
                 + juce::String (maxStep, 4) + ")");
    }

    // --- channel layouts ---------------------------------------------------
    {
        KeyGloProcessor p;
        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (stereo), "stereo/stereo supported");

        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.checkBusesLayoutSupported (mono), "mono/mono supported");

        juce::AudioProcessor::BusesLayout mismatched;
        mismatched.inputBuses.add (juce::AudioChannelSet::stereo());
        mismatched.outputBuses.add (juce::AudioChannelSet::mono());
        check (! p.checkBusesLayoutSupported (mismatched), "stereo->mono rejected");
    }

    // --- state round-trip --------------------------------------------------
    {
        KeyGloProcessor a;
        setParam (a, pid::rangeSense, 0.31f);
        setParam (a, pid::fineTuneCents, -17.5f);
        setParam (a, pid::transposeSemitones, 7.0f);   // "+3"
        setParam (a, pid::sampleSolo, 1.0f);
        a.setSavedUIScale (1.25f);
        a.setReduceMotion (true);

        juce::MemoryBlock state;
        a.getStateInformation (state);

        KeyGloProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());

        checkNear (b.getAPVTS().getRawParameterValue (pid::rangeSense)->load(),
                   0.31, 1.0e-3, "rangeSense round-trips");
        checkNear (b.getAPVTS().getRawParameterValue (pid::fineTuneCents)->load(),
                   -17.5, 1.0e-2, "fineTuneCents round-trips");
        checkNear (b.getAPVTS().getRawParameterValue (pid::transposeSemitones)->load(),
                   7.0, 1.0e-3, "transpose choice round-trips");
        check (b.getAPVTS().getRawParameterValue (pid::sampleSolo)->load() > 0.5f,
               "sampleSolo round-trips");
        checkNear (b.getSavedUIScale(), 1.25, 1.0e-3, "windowScale round-trips");
        check (b.getReduceMotion(), "reduceMotion round-trips");
    }

    // --- display model publish/read ----------------------------------------
    {
        AnalysisDisplayModel model;
        auto initial = model.get();
        check (initial != nullptr, "model publishes an initial snapshot");
        check (initial->key == "F#" && initial->scale == "Minor",
               "initial snapshot carries the contract dataset");
        check (initial->rootNote == 6 && initial->scaleNotes[6],
               "F# is the root and in scale");
        check (! initial->scaleNotes[0], "C is out of the F# minor scale");

        auto next = std::make_shared<AnalysisSnapshot>();
        next->key = "A";
        model.publish (next);
        check (model.get()->key == "A", "publish replaces the snapshot");
    }

    // --- demo feed determinism ---------------------------------------------
    {
        AnalysisSnapshot base;
        DemoFeed f1, f2;
        for (int i = 0; i < 200; ++i)
        {
            f1.tick (1.0 / 60.0, base);
            f2.tick (1.0 / 60.0, base);
        }
        bool identical = true;
        for (int i = 0; i < 12; ++i)
            identical = identical && f1.chroma[(size_t) i] == f2.chroma[(size_t) i];
        for (int i = 0; i < DemoFeed::trailLength; ++i)
            identical = identical && f1.trailMidi[(size_t) i] == f2.trailMidi[(size_t) i];
        check (identical, "demo feed is deterministic");

        int voiced = 0, gaps = 0;
        for (int i = 0; i < DemoFeed::trailLength; ++i)
            (f1.trailMidi[(size_t) i] > 0.0f ? voiced : gaps)++;
        check (voiced > DemoFeed::trailLength / 2, "trail is mostly voiced");
        check (gaps > 10, "trail contains unvoiced gaps");
    }

    // --- headless editor ---------------------------------------------------
    {
        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr, "createEditor returns an editor");

        if (auto* kg = dynamic_cast<KeyGloEditor*> (editor.get()))
        {
            editor->setSize (Design::width, Design::height);
            for (int i = 0; i < 10; ++i)
                kg->refreshDisplays();

            juce::Image image (juce::Image::ARGB, Design::width, Design::height, true);
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);

            check (Assets::loadFailureCount() == 0,
                   "no asset load failures (" + Assets::describeFailures() + ")");

            // Required component architecture, findable by name.
            const char* required[] = { "Header", "BeatAnalysis", "KeyWheelHUD",
                                       "ArtistRange", "AutoTuneSetup",
                                       "TransposePreview", "SampleTune",
                                       "MacroStrip", "Footer", "TunerDial" };
            std::function<bool (juce::Component&, const juce::String&)> findByName =
                [&] (juce::Component& c, const juce::String& name) -> bool
            {
                if (c.getName() == name)
                    return true;
                for (auto* child : c.getChildren())
                    if (findByName (*child, name))
                        return true;
                return false;
            };
            for (const char* name : required)
                check (findByName (*editor, name),
                       juce::String ("component present: ") + name);

            // The rendered frame must not be empty or flat: sample a few
            // pixels that should differ (shell vs cyan accents).
            const auto centre = image.getPixelAt (746, 320);
            check (image.getPixelAt (10, 10) != juce::Colour(), "corner painted");
            check (centre.getBrightness() > 0.02f, "wheel area painted");
        }

        // Open/close cycles must not crash.
        for (int i = 0; i < 3; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditor());
            e2->setSize (Design::width, Design::height);
        }
        check (true, "editor open/close cycles survive");
    }

    std::printf ("%d checks, %d failed\n", checksRun, checksFailed);
    return checksFailed == 0 ? 0 : 1;
}
