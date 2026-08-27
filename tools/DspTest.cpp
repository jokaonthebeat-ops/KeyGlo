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
#include "analysis/BeatKeyDetector.h"
#include "analysis/TempoDetector.h"
#include "analysis/CaptureRing.h"
#include "analysis/PitchTracker.h"
#include "analysis/VocalRangeProfiler.h"
#include "analysis/HookFitScorer.h"
#include "analysis/SamplePitchDetector.h"
#include "state/ArtistProfileStore.h"
#include "state/AppPaths.h"
#include "dsp/PreviewPitchShifter.h"
#include "presets/PresetManager.h"
#include "ui/AutoTuneSetupPanel.h"
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
            { pid::fineTuneCents, 0.0f },   // deviation: json's +4 is a demo value
            { pid::outputGainDb, -2.0f },
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
            // Documented deviation from parameters.json (default "-2"):
            // a fresh instance must never transpose the program.
            check (transpose->getIndex() == 4, "transpose default is Original");
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

    // =======================================================================
    //  Milestone 2 - beat analysis engine, ground-truth fixtures
    // =======================================================================

    // Chord-loop synthesiser: sustained triads (3 harmonics each) over a bass
    // drone, tonic chord weighted double. All deterministic.
    struct Chord { float notes[3]; };
    auto renderLoop = [] (const std::vector<Chord>& chords, float bassHz,
                          double seconds, double sr, double detuneCents) -> std::vector<float>
    {
        const double ratio = std::pow (2.0, detuneCents / 1200.0);
        const int n = (int) (sr * seconds);
        std::vector<float> out ((size_t) n, 0.0f);
        const double chordLen = seconds / (double) chords.size();

        for (size_t c = 0; c < chords.size(); ++c)
        {
            const int start = (int) (sr * chordLen * (double) c);
            const int end = juce::jmin (n, (int) (sr * chordLen * (double) (c + 1)));
            for (float note : chords[c].notes)
            {
                if (note <= 0.0f)
                    continue;
                for (int h = 1; h <= 3; ++h)
                {
                    const double f = note * ratio * h;
                    const double amp = 0.16 / h;
                    for (int i = start; i < end; ++i)
                        out[(size_t) i] += (float) (amp * std::sin (juce::MathConstants<double>::twoPi
                                                                      * f * (i - start) / sr));
                }
            }
        }
        // Bass drone (root evidence).
        for (int i = 0; i < n; ++i)
            out[(size_t) i] += (float) (0.30 * std::sin (juce::MathConstants<double>::twoPi
                                                           * bassHz * ratio * i / sr));

        // Normalise to 0.7 peak: chords + drone can sum past full scale, and
        // a fixture that clips when written to a 16-bit WAV grows harmonics
        // that wreck the very in-scale gate it exists to exercise.
        float peak = 0.0f;
        for (auto v : out)
            peak = juce::jmax (peak, std::abs (v));
        if (peak > 1.0e-6f)
            for (auto& v : out)
                v *= 0.7f / peak;
        return out;
    };

    // F# natural minor: i (x2, tonic emphasis), VI, iv, VII over an F#1 drone.
    const std::vector<Chord> fsharpMinor = {
        { { 92.5f, 110.0f, 138.59f } },      // F#m
        { { 92.5f, 110.0f, 138.59f } },
        { { 146.83f, 185.0f, 220.0f } },     // D
        { { 123.47f, 146.83f, 185.0f } },    // Bm
        { { 164.81f, 207.65f, 246.94f } },   // E
        { { 92.5f, 110.0f, 138.59f } },      // F#m
    };
    // A major: I (x2), IV, V over an A1 drone.
    const std::vector<Chord> aMajor = {
        { { 110.0f, 138.59f, 164.81f } },    // A
        { { 110.0f, 138.59f, 164.81f } },
        { { 146.83f, 185.0f, 220.0f } },     // D
        { { 164.81f, 207.65f, 246.94f } },   // E
        { { 110.0f, 138.59f, 164.81f } },    // A
    };

    {
        const auto audio = renderLoop (fsharpMinor, 46.25f, 10.0, 44100.0, 0.0);
        const auto r = BeatKeyDetector::analyse (audio.data(), (int) audio.size(), 44100.0);
        check (r.reliable, "F# minor loop: reliable");
        check (r.rootPc == 6 && r.minor,
               "F# minor loop detected as F# minor (got " + r.keyName() + " " + r.scaleName()
                 + ", conf " + juce::String (r.confidence, 2) + ")");
        checkNear (r.tuningCents, 0.0, 6.0, "F# minor loop: tuning near zero");
        check (r.confidence > 0.5f, "F# minor loop: confident ("
                 + juce::String (r.confidence, 2) + ")");
    }

    {
        const auto audio = renderLoop (aMajor, 55.0f, 10.0, 44100.0, 0.0);
        const auto r = BeatKeyDetector::analyse (audio.data(), (int) audio.size(), 44100.0);
        check (r.reliable && r.rootPc == 9 && ! r.minor,
               "A major loop detected as A major (got " + r.keyName() + " " + r.scaleName() + ")");
    }

    {
        // The relative-key tie: A-major chord material over an F#1 drone.
        // The bass register is the tonic evidence; expect F# minor.
        const auto audio = renderLoop (aMajor, 46.25f, 10.0, 44100.0, 0.0);
        const auto r = BeatKeyDetector::analyse (audio.data(), (int) audio.size(), 44100.0);
        check (r.reliable && r.rootPc == 6 && r.minor,
               "A-major chords over F# bass resolve to F# minor (got "
                 + r.keyName() + " " + r.scaleName() + ")");
    }

    {
        // +30 cents global detune must be measured, and the key still found.
        const auto audio = renderLoop (fsharpMinor, 46.25f, 10.0, 44100.0, 30.0);
        const auto r = BeatKeyDetector::analyse (audio.data(), (int) audio.size(), 44100.0);
        checkNear (r.tuningCents, 30.0, 10.0, "detuned loop: +30 cents measured");
        check (r.reliable && r.rootPc == 6 && r.minor,
               "detuned loop still detects F# minor (got " + r.keyName() + " " + r.scaleName() + ")");
    }

    {
        // A produced groove, not clean sines: 808 bass with kick AM/FM,
        // triads with a 2nd harmonic, filtered-noise bed, chords changing
        // per bar at 148 BPM (the uishot signal feed, pinned as a fixture -
        // this exact material once failed the tonality gate).
        juce::Random random (0x4b47);
        const double sr = 48000.0;
        const int n = (int) (sr * 8.5);
        std::vector<float> groove ((size_t) n, 0.0f);
        double bassPhase = 0.0, chordPhase[3] = { 0.0, 0.0, 0.0 };
        float lp = 0.0f;
        static const double bassHz[4] = { 46.25, 36.71, 61.74, 41.20 };
        static const double triads[4][3] = { { 185.0, 220.0, 277.18 },
                                             { 146.83, 185.0, 220.0 },
                                             { 123.47, 146.83, 185.0 },
                                             { 164.81, 207.65, 246.94 } };
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double beatLen = 60.0 / 148.0;
            const int chord = ((int) (t / (beatLen * 4.0))) % 4;
            const float kick = (float) std::exp (-std::fmod (t, beatLen) * 9.0);
            bassPhase += 2.0 * juce::MathConstants<double>::pi
                           * (bassHz[chord] + 7.0 * kick) / sr;
            float v = 0.48f * (0.45f + 0.55f * kick) * (float) std::sin (bassPhase);
            for (int c = 0; c < 3; ++c)
            {
                chordPhase[c] += 2.0 * juce::MathConstants<double>::pi * triads[chord][c] / sr;
                v += 0.16f * (float) std::sin (chordPhase[c])
                   + 0.05f * (float) std::sin (2.0 * chordPhase[c]);
            }
            const float white = random.nextFloat() * 2.0f - 1.0f;
            lp += 0.12f * (white - lp);
            groove[(size_t) i] = v + 0.035f * lp;
        }

        const auto r = BeatKeyDetector::analyse (groove.data(), n, sr);
        check (r.reliable, "produced groove: reliable despite kick modulation + noise bed"
                 " (peakShare " + juce::String (r.gatePeakShare, 3)
                 + ", spread " + juce::String (r.gateSpread, 2)
                 + ", bestR " + juce::String (r.gateBestR, 2)
                 + ", conf " + juce::String (r.confidence, 2) + ")");
        check (r.rootPc == 6 && r.minor,
               "produced groove detected as F# minor (got " + r.keyName() + " "
                 + r.scaleName() + ", conf " + juce::String (r.confidence, 2) + ")");

        const auto t = TempoDetector::analyse (groove.data(), n, sr);
        check (t.reliable && std::abs (t.bpm - 148.0f) < 2.0f,
               "produced groove tempo reports the beat level, 148 (got "
                 + juce::String (t.bpm, 1) + ")");
    }

    {
        // Noise + clicks have no key. Honesty gate: reliable must be false.
        juce::Random rng (0x4b47);
        std::vector<float> noise ((size_t) (44100 * 8), 0.0f);
        for (auto& v : noise)
            v = 0.4f * (rng.nextFloat() * 2.0f - 1.0f);
        const auto r = BeatKeyDetector::analyse (noise.data(), (int) noise.size(), 44100.0);
        check (! r.reliable, "white noise: no reliable key");
        std::printf ("        [noise gates: peakShare %.3f inScale %.2f bestR %.2f conf %.2f]\n",
                     r.gatePeakShare, r.gateSpread, r.gateBestR, r.confidence);
    }

    // --- tempo --------------------------------------------------------------
    auto renderClicks = [] (double bpm, double seconds, double sr) -> std::vector<float>
    {
        juce::Random rng (0x7717);
        const int n = (int) (sr * seconds);
        std::vector<float> out ((size_t) n, 0.0f);
        const double beatLen = 60.0 / bpm * sr;
        for (double pos = 0.0; pos < n - 300; pos += beatLen)
            for (int i = 0; i < 260; ++i)
                out[(size_t) ((int) pos + i)] += 0.8f * (rng.nextFloat() * 2.0f - 1.0f)
                                                   * (float) std::exp (-i / 60.0);
        return out;
    };

    for (const double wantBpm : { 96.0, 120.0, 148.0 })
    {
        const auto clicks = renderClicks (wantBpm, 12.0, 44100.0);
        const auto t = TempoDetector::analyse (clicks.data(), (int) clicks.size(), 44100.0);
        check (t.reliable, juce::String (wantBpm, 0) + " BPM clicks: reliable");

        bool anyCandidate = false;
        for (auto c : t.candidates)
            anyCandidate = anyCandidate || std::abs (c - wantBpm) < 2.0f;
        check (anyCandidate,
               juce::String (wantBpm, 0) + " BPM within 2 of a candidate (main "
                 + juce::String (t.bpm, 1) + ")");

        const bool mainOk = std::abs (t.bpm - wantBpm) < 2.0f
                          || std::abs (t.bpm - wantBpm * 2.0) < 3.0f
                          || std::abs (t.bpm - wantBpm * 0.5) < 1.5f;
        check (mainOk, juce::String (wantBpm, 0) + " BPM main estimate at a valid metrical level (got "
                 + juce::String (t.bpm, 1) + ")");
    }

    {
        std::vector<float> silence ((size_t) (44100 * 8), 0.0f);
        const auto t = TempoDetector::analyse (silence.data(), (int) silence.size(), 44100.0);
        check (! t.reliable, "silence: no tempo claimed");
    }

    // --- a fresh instance never pitch-shifts on its own ----------------------
    {
        KeyGloProcessor p;
        check (! p.getDisplayModel().previewArmed(),
               "fresh instance: preview shifter is disarmed");
    }

    // --- capture ring -------------------------------------------------------
    {
        CaptureRing ring;
        ring.prepare (48000.0, 2);

        // Perfectly out-of-phase stereo: mono sum cancels, but the honest
        // silence gate must still see the energy per channel.
        juce::AudioBuffer<float> buf (2, 4800);
        for (int i = 0; i < 4800; ++i)
        {
            const float v = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * i / 48000.0f);
            buf.setSample (0, i, v);
            buf.setSample (1, i, -v);
        }
        for (int b = 0; b < 20; ++b)
            ring.write (buf);

        std::vector<float> mono;
        float channelRms = 0.0f;
        const int got = ring.readLatestMono (mono, 48000, channelRms);
        check (got == 48000, "ring returns requested history");
        check (channelRms > 0.3f, "out-of-phase stereo is NOT gated as silence (rms "
                 + juce::String (channelRms, 3) + ")");
        float monoPeak = 0.0f;
        for (auto v : mono) monoPeak = juce::jmax (monoPeak, std::abs (v));
        check (monoPeak < 0.01f, "mono sum of out-of-phase stereo cancels as expected");
    }

    // --- end-to-end: processor feeds ring, coordinator publishes ------------
    {
        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 44100.0, 512);
        p.prepareToPlay (44100.0, 512);

        const auto audio = renderLoop (fsharpMinor, 46.25f, 9.0, 44100.0, 0.0);
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= (int) audio.size(); start += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                block.setSample (0, i, audio[(size_t) (start + i)]);
                block.setSample (1, i, audio[(size_t) (start + i)]);
            }
            p.processBlock (block, midi);
        }

        p.analyseCaptureNow();
        bool published = false;
        for (int tries = 0; tries < 300 && ! published; ++tries)
        {
            juce::Thread::sleep (50);
            auto snap = p.getDisplayModel().get();
            published = snap->hasBeatResult && ! snap->analyzing;
        }
        auto snap = p.getDisplayModel().get();
        check (published, "live-ring analysis publishes a result");
        check (! snap->noReliableKey && snap->key == "F#" && snap->scale == "Minor",
               "live-ring analysis finds F# minor (got " + snap->key + " " + snap->scale + ")");
        check (snap->sourceName == "LIVE INPUT", "live result labelled LIVE INPUT");

        // Fast lane published real visuals with bass energy present.
        auto live = p.getDisplayModel().getLive();
        check (live->active, "live visuals active after audio");
        check (live->chroma[6] > 0.5f, "live chroma sees F# energy ("
                 + juce::String (live->chroma[6], 2) + ")");
    }

    // --- file path end-to-end ----------------------------------------------
    {
        const auto audio = renderLoop (aMajor, 55.0f, 9.0, 44100.0, 0.0);
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("keyglo-test-amajor.wav");
        file.deleteFile();
        {
            juce::WavAudioFormat wav;
            auto stream = file.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
            check (writer != nullptr, "test wav writer created");
            if (writer != nullptr)
            {
                stream.release();
                juce::AudioBuffer<float> b (1, (int) audio.size());
                for (int i = 0; i < (int) audio.size(); ++i)
                    b.setSample (0, i, audio[(size_t) i]);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }

        KeyGloProcessor p;
        p.analyseFileAsync (file);
        bool published = false;
        for (int tries = 0; tries < 300 && ! published; ++tries)
        {
            juce::Thread::sleep (50);
            auto snap = p.getDisplayModel().get();
            published = snap->hasBeatResult && ! snap->analyzing;
        }
        auto snap = p.getDisplayModel().get();
        check (published, "file analysis publishes a result");
        check (! snap->noReliableKey && snap->key == "A" && snap->scale == "Major",
               "file analysis finds A major (got " + snap->key + " " + snap->scale + ")");
        check (snap->sourceName == file.getFileName(), "file result carries the file name");
        check (snap->bpm < 1.0f || snap->bpmSource != "HOST",
               "file BPM never claims host tempo");
        file.deleteFile();
    }

    // =======================================================================
    //  Milestone 3 - vocal range, profiles and hook fit
    // =======================================================================

    // A sung note: harmonic-rich (voice-like), vibrato, soft attack/release.
    auto renderVoice = [] (float midi, double seconds, double sr,
                           float vibratoCents = 25.0f) -> std::vector<float>
    {
        const int n = (int) (sr * seconds);
        std::vector<float> out ((size_t) n, 0.0f);
        const double f0 = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
        double phase[6] = { 0, 0, 0, 0, 0, 0 };
        const double amps[6] = { 1.0, 0.55, 0.32, 0.18, 0.10, 0.06 };

        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double vib = std::pow (2.0, (vibratoCents * std::sin (juce::MathConstants<double>::twoPi
                                                                          * 5.2 * t)) / 1200.0);
            const float env = (float) (juce::jmin (1.0, t / 0.05)
                                        * juce::jmin (1.0, (seconds - t) / 0.05));
            float v = 0.0f;
            for (int h = 0; h < 6; ++h)
            {
                phase[h] += juce::MathConstants<double>::twoPi * f0 * vib * (h + 1) / sr;
                v += (float) (amps[h] * std::sin (phase[h]));
            }
            out[(size_t) i] = 0.22f * env * v;
        }
        return out;
    };

    // Feed audio to a processor at roughly real time. The vocal worker caps
    // how much backlog it will pitch-track per pass (deliberately - a stalled
    // worker must not stampede), so a test that dumps ten seconds instantly
    // would have most of it skipped. Real hosts deliver in real time; so does
    // this.
    auto feedPaced = [] (KeyGloProcessor& proc, const std::vector<float>& audio,
                         double sr, int blockSize = 512)
    {
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        const double blockMs = 1000.0 * blockSize / sr;
        double budget = 0.0;

        for (int start = 0; start + blockSize <= (int) audio.size(); start += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                block.setSample (0, i, audio[(size_t) (start + i)]);
                block.setSample (1, i, audio[(size_t) (start + i)]);
            }
            proc.processBlock (block, midi);

            // Sleep in ~10 ms grains rather than per block, so the pacing
            // costs wall-clock time without thousands of tiny sleeps.
            budget += blockMs;
            if (budget >= 10.0)
            {
                juce::Thread::sleep ((int) budget);
                budget = 0.0;
            }
        }
    };

    // --- pitch tracker ------------------------------------------------------
    {
        for (const float midi : { 45.0f, 55.0f, 60.0f, 67.0f, 72.0f })
        {
            const auto voice = renderVoice (midi, 1.0, 48000.0, 0.0f);
            const auto frames = PitchTracker::analyseBuffer (voice.data(), (int) voice.size(),
                                                             48000.0);
            std::vector<float> voiced;
            for (const auto& f : frames)
                if (f.voiced)
                    voiced.push_back (f.midi);

            check (voiced.size() > frames.size() / 2,
                   "MIDI " + juce::String (midi, 0) + ": mostly voiced ("
                     + juce::String ((int) voiced.size()) + "/" + juce::String ((int) frames.size()) + ")");
            if (! voiced.empty())
            {
                const float median = VocalRangeProfiler::percentile (voiced, 0.5f);
                checkNear (median, midi, 0.35, "MIDI " + juce::String (midi, 0) + " tracked");
            }
        }
    }

    {
        // Breath/noise must NOT become notes - a gap, never a false zero.
        juce::Random rng (0x9911);
        std::vector<float> breath ((size_t) (48000 * 1.5), 0.0f);
        float lp = 0.0f;
        for (auto& v : breath)
        {
            lp += 0.05f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            v = 0.05f * lp;
        }
        const auto frames = PitchTracker::analyseBuffer (breath.data(), (int) breath.size(), 48000.0);
        int voicedCount = 0;
        for (const auto& f : frames)
            if (f.voiced)
                ++voicedCount;
        check (voicedCount < (int) frames.size() / 5,
               "filtered noise is mostly unvoiced (" + juce::String (voicedCount) + "/"
                 + juce::String ((int) frames.size()) + ")");

        std::vector<float> silence ((size_t) (48000 * 1.0), 0.0f);
        const auto quiet = PitchTracker::analyseBuffer (silence.data(), (int) silence.size(), 48000.0);
        int quietVoiced = 0;
        for (const auto& f : quiet)
            if (f.voiced)
                ++quietVoiced;
        check (quietVoiced == 0, "digital silence produces no voiced frames");
    }

    // --- range profiler -----------------------------------------------------
    {
        // A baritone: comfortable G2..C4, strong C3..G3, extremes E2/E4.
        auto spread = [] (float centre, float halfWidth, int count)
        {
            std::vector<float> v;
            for (int i = 0; i < count; ++i)
                v.push_back (centre + halfWidth * (2.0f * (float) i / (float) (count - 1) - 1.0f));
            return v;
        };

        const auto low  = spread (44.0f, 3.0f, 100);   // around G#2
        const auto mid  = spread (55.0f, 5.0f, 100);   // around G3
        const auto high = spread (64.0f, 3.0f, 100);   // around E4
        const auto profile = VocalRangeProfiler::build (low, mid, high, {});

        check (profile.extendedLowMidi <= profile.comfortableLowMidi,
               "profile: extended low <= comfortable low");
        check (profile.comfortableLowMidi <= profile.strongLowMidi,
               "profile: comfortable low <= strong low");
        check (profile.strongLowMidi < profile.strongHighMidi,
               "profile: strong zone is non-empty");
        check (profile.strongHighMidi <= profile.comfortableHighMidi,
               "profile: strong high <= comfortable high");
        check (profile.comfortableHighMidi <= profile.extendedHighMidi,
               "profile: comfortable high <= extended high");
        check (! profile.hasFalsetto, "profile: no falsetto when the phase is skipped");
        checkNear (profile.strongCentre(), 55.0, 2.5, "profile: strong centre near the mid phase");

        // One wild outlier must not define the range (percentiles, not extremes).
        auto withOutlier = high;
        withOutlier.push_back (96.0f);   // an accidental squeak
        const auto robust = VocalRangeProfiler::build (low, mid, withOutlier, {});
        check (robust.extendedHighMidi < 75,
               "profile: a single outlier does not stretch the range (got "
                 + juce::String (robust.extendedHighMidi) + ")");

        const auto falsetto = spread (76.0f, 2.0f, 60);
        const auto withFalsetto = VocalRangeProfiler::build (low, mid, high, falsetto);
        check (withFalsetto.hasFalsetto && withFalsetto.falsettoHighMidi > withFalsetto.extendedHighMidi,
               "profile: falsetto phase extends the top");
    }

    // --- hook fit -----------------------------------------------------------
    {
        ArtistProfile p;
        p.extendedLowMidi = 48; p.comfortableLowMidi = 52;
        p.strongLowMidi = 55;   p.strongHighMidi = 64;
        p.comfortableHighMidi = 67; p.extendedHighMidi = 71;
        p.falsettoHighMidi = 71;

        auto hookAt = [] (std::initializer_list<int> midiNotes, float secondsEach)
        {
            HookStats s;
            std::vector<float> all;
            for (int m : midiNotes)
            {
                s.noteSeconds[(size_t) m] += secondsEach;
                s.totalVoicedSeconds += secondsEach;
                all.push_back ((float) m);
            }
            s.medianMidi = VocalRangeProfiler::percentile (all, 0.5f);
            return s;
        };

        std::array<bool, 12> anyScale;
        anyScale.fill (true);

        // A hook sitting squarely in the strong zone needs no transposition.
        {
            const auto hook = hookAt ({ 55, 57, 59, 60, 62, 64 }, 0.5f);
            const auto fit = HookFitScorer::score (hook, p, anyScale, true);
            check (fit.valid, "hook fit: in-zone hook scores");
            check (fit.recommendedTranspose == 0,
                   "hook fit: in-zone hook recommends no shift (got "
                     + juce::String (fit.recommendedTranspose) + ")");
            check (fit.artistFit > 0.7f, "hook fit: in-zone hook scores high ("
                     + juce::String (fit.artistFit, 2) + ")");
        }

        // A hook four semitones too high should be pulled DOWN.
        {
            const auto hook = hookAt ({ 67, 69, 71, 72, 74 }, 0.5f);
            const auto fit = HookFitScorer::score (hook, p, anyScale, true);
            check (fit.recommendedTranspose < 0,
                   "hook fit: a too-high hook is transposed down (got "
                     + juce::String (fit.recommendedTranspose) + ")");
            check (fit.estimatedFit > fit.artistFit,
                   "hook fit: the recommendation beats the original ("
                     + juce::String (fit.estimatedFit, 2) + " vs "
                     + juce::String (fit.artistFit, 2) + ")");
        }

        // ...and a too-low hook pushed UP.
        {
            const auto hook = hookAt ({ 47, 48, 50, 52 }, 0.5f);
            const auto fit = HookFitScorer::score (hook, p, anyScale, true);
            check (fit.recommendedTranspose > 0,
                   "hook fit: a too-low hook is transposed up (got "
                     + juce::String (fit.recommendedTranspose) + ")");
        }

        // Scale compatibility: a hook of scale tones beats one full of
        // out-of-scale tones against the same beat.
        {
            std::array<bool, 12> cMajor { true, false, true, false, true, true,
                                          false, true, false, true, false, true };
            const auto inScale  = hookAt ({ 60, 62, 64, 65, 67 }, 0.5f);
            const auto outScale = hookAt ({ 61, 63, 66, 68, 70 }, 0.5f);
            const auto a = HookFitScorer::score (inScale, p, cMajor, true);
            const auto b = HookFitScorer::score (outScale, p, cMajor, true);
            check (a.hookMatch > 0.9f, "hook fit: in-scale hook matches the beat ("
                     + juce::String (a.hookMatch, 2) + ")");
            check (b.hookMatch < 0.1f, "hook fit: out-of-scale hook does not ("
                     + juce::String (b.hookMatch, 2) + ")");
        }

        // Too little voiced material must not produce a confident score.
        {
            const auto tiny = hookAt ({ 60 }, 0.3f);
            const auto fit = HookFitScorer::score (tiny, p, anyScale, true);
            check (! fit.valid, "hook fit: refuses to score a fragment");
        }

        // Ties prefer the smaller shift.
        {
            const auto hook = hookAt ({ 59, 60 }, 1.0f);
            const auto fit = HookFitScorer::score (hook, p, anyScale, true);
            check (std::abs (fit.recommendedTranspose) <= 2,
                   "hook fit: prefers a small shift when scores are close (got "
                     + juce::String (fit.recommendedTranspose) + ")");
        }
    }

    // --- profile persistence -------------------------------------------------
    {
        auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("KeyGloTestProfiles");
        sandbox.deleteRecursively();
        ArtistProfileStore::dirOverride() = sandbox;

        ArtistProfile p;
        p.extendedLowMidi = 41; p.comfortableLowMidi = 45;
        p.strongLowMidi = 50;   p.strongHighMidi = 61;
        p.comfortableHighMidi = 65; p.extendedHighMidi = 70;
        p.falsettoHighMidi = 79; p.hasFalsetto = true;

        check (ArtistProfileStore::save ("Test Artist", p), "profile saves");

        ArtistProfile loaded;
        check (ArtistProfileStore::load ("Test Artist", loaded), "profile loads");
        check (loaded.extendedLowMidi == 41 && loaded.comfortableLowMidi == 45
                 && loaded.strongLowMidi == 50 && loaded.strongHighMidi == 61
                 && loaded.comfortableHighMidi == 65 && loaded.extendedHighMidi == 70
                 && loaded.falsettoHighMidi == 79 && loaded.hasFalsetto,
               "profile round-trips every field");

        check (ArtistProfileStore::listProfiles().contains ("Test Artist"),
               "saved profile is listed");
        check (! ArtistProfileStore::load ("Nobody", loaded), "missing profile fails cleanly");

        // A name with path characters must not escape the profiles folder.
        check (ArtistProfileStore::save ("../../evil", p), "awkward name still saves");
        check (ArtistProfileStore::fileFor ("../../evil").getParentDirectory() == sandbox,
               "profile names cannot escape the profiles directory");

        sandbox.deleteRecursively();
        ArtistProfileStore::dirOverride() = juce::File();
    }

    // --- end-to-end: sung audio through the processor ------------------------
    {
        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        // Give the engine a profile directly (the guided test is UI-driven).
        ArtistProfile profile;
        profile.extendedLowMidi = 48; profile.comfortableLowMidi = 52;
        profile.strongLowMidi = 55;   profile.strongHighMidi = 64;
        profile.comfortableHighMidi = 67; profile.extendedHighMidi = 71;
        profile.falsettoHighMidi = 71;
        p.getVocalEngine().setProfile (profile);

        // Sing a short phrase in the strong zone.
        std::vector<float> phrase;
        for (int midi : { 60, 62, 64, 62, 60, 59 })
        {
            const auto note = renderVoice ((float) midi, 0.7, 48000.0);
            phrase.insert (phrase.end(), note.begin(), note.end());
        }

        feedPaced (p, phrase, 48000.0);

        bool tracked = false;
        for (int tries = 0; tries < 200 && ! tracked; ++tries)
        {
            juce::Thread::sleep (25);
            auto live = p.getDisplayModel().getLive();
            int voicedPoints = 0;
            for (float v : live->pitchTrail)
                if (v > 0.0f)
                    ++voicedPoints;
            tracked = voicedPoints > 60;
        }
        check (tracked, "sung phrase fills the live pitch trail");

        bool scored = false;
        for (int tries = 0; tries < 200 && ! scored; ++tries)
        {
            juce::Thread::sleep (25);
            scored = p.getDisplayModel().get()->hasFitResult;
        }
        auto snap = p.getDisplayModel().get();
        check (scored, "sung phrase produces a fit result");
        check (snap->rangeFit > 0.5f, "in-zone phrase scores well ("
                 + juce::String (snap->rangeFit, 2) + ")");
        check (std::abs (snap->recommendedTranspose) <= 1,
               "in-zone phrase needs little or no transposition (got "
                 + juce::String (snap->recommendedTranspose) + ")");
    }

    // --- hook match must not stay stale at 0 once a key appears -------------
    {
        // Scoring happens continuously; a fit computed BEFORE any key exists
        // stores hookMatch = 0. When the key arrives the pod must be re-scored,
        // or it reads "0" (a terrible match) instead of the truth.
        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        ArtistProfile profile;
        profile.extendedLowMidi = 48; profile.comfortableLowMidi = 52;
        profile.strongLowMidi = 55;   profile.strongHighMidi = 64;
        profile.comfortableHighMidi = 67; profile.extendedHighMidi = 71;
        profile.falsettoHighMidi = 71;
        p.getVocalEngine().setProfile (profile);

        // Sing C-major-only material, then let the beat analysis run on it.
        std::vector<float> phrase;
        for (int m : { 60, 62, 64, 65, 67, 65, 64, 62 })
        {
            const auto note = renderVoice ((float) m, 0.9, 48000.0);
            phrase.insert (phrase.end(), note.begin(), note.end());
        }

        feedPaced (p, phrase, 48000.0);

        p.analyseCaptureNow();
        for (int tries = 0; tries < 300; ++tries)
        {
            juce::Thread::sleep (50);
            auto s = p.getDisplayModel().get();
            if (s->hasBeatResult && ! s->analyzing && s->hasFitResult)
                break;
        }

        auto snap = p.getDisplayModel().get();
        check (snap->hasFitResult, "sung material produces a fit");
        check (snap->hasProfile,
               "snapshot reports the profile the engine holds (SAVE PROFILE enabled)");
        if (snap->hasBeatResult && ! snap->noReliableKey)
            check (snap->hookMatch > 0.5f,
                   "hook match is re-scored once a key exists, not left at 0 (got "
                     + juce::String (snap->hookMatch, 2) + " against "
                     + snap->key + " " + snap->scale + ")");
    }

    // --- a vocal take must not overwrite a file-analysed beat ---------------
    {
        // Write a beat file, analyse it, then sing over it: the key must
        // still describe the BEAT, not the singer.
        const auto beat = renderLoop (aMajor, 55.0f, 9.0, 44100.0, 0.0);
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("keyglo-test-beat.wav");
        file.deleteFile();
        {
            juce::WavAudioFormat wav;
            auto stream = file.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
            if (writer != nullptr)
            {
                stream.release();
                juce::AudioBuffer<float> b (1, (int) beat.size());
                for (int i = 0; i < (int) beat.size(); ++i)
                    b.setSample (0, i, beat[(size_t) i]);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }

        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.analyseFileAsync (file);
        for (int tries = 0; tries < 300; ++tries)
        {
            juce::Thread::sleep (50);
            auto s = p.getDisplayModel().get();
            if (s->hasBeatResult && ! s->analyzing)
                break;
        }
        check (p.getDisplayModel().get()->key == "A", "beat file analysed as A major");

        // Now push 10 s of singing through - long enough that the old
        // auto-re-analysis would have fired twice.
        std::vector<float> singing;
        for (int m : { 64, 66, 68, 69, 71, 69, 68, 66 })
        {
            const auto note = renderVoice ((float) m, 1.3, 48000.0);
            singing.insert (singing.end(), note.begin(), note.end());
        }
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= (int) singing.size(); start += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                block.setSample (0, i, singing[(size_t) (start + i)]);
                block.setSample (1, i, singing[(size_t) (start + i)]);
            }
            p.processBlock (block, midi);
        }
        juce::Thread::sleep (4000);   // past two auto-analysis windows

        auto after = p.getDisplayModel().get();
        check (after->key == "A" && after->sourceName == file.getFileName(),
               "singing over an analysed beat does not replace its key (still "
                 + after->key + " from " + after->sourceName + ")");
        file.deleteFile();
    }

    // =======================================================================
    //  Milestone 4 - 808/sample tuning and the preview shifter
    // =======================================================================

    // An 808: pitch-drop attack into a stable sustain, exponential decay.
    auto render808 = [] (float sustainMidi, double seconds, double sr,
                         float detuneCents = 0.0f, float glideSemis = 0.0f) -> std::vector<float>
    {
        const int n = (int) (sr * seconds);
        std::vector<float> out ((size_t) n, 0.0f);
        const double f0 = 440.0 * std::pow (2.0, (sustainMidi - 69.0 + detuneCents / 100.0f) / 12.0);
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            // Attack pitch drop over 25 ms, plus an optional slow glide that
            // makes the whole sample a pitch envelope.
            const double attackSweep = 18.0 * std::exp (-t / 0.012);
            const double glide = std::pow (2.0, (glideSemis * juce::jmin (1.0, t / (seconds * 0.8))) / 12.0);
            phase += juce::MathConstants<double>::twoPi * (f0 * glide + attackSweep) / sr;
            const float env = (float) ((1.0 - std::exp (-t / 0.004)) * std::exp (-t / (seconds * 0.35)));
            out[(size_t) i] = 0.7f * env * (float) (std::sin (phase)
                                + 0.25 * std::sin (2.0 * phase));
        }
        return out;
    };

    std::array<bool, 12> noScale {};

    // --- sample pitch detection ---------------------------------------------
    {
        // F#1 808 at concert pitch: detected within 10 cents, no envelope.
        const auto kick = render808 (30.0f, 1.2, 48000.0);   // F#1 = MIDI 30
        const auto r = SamplePitchDetector::analyse (kick.data(), (int) kick.size(),
                                                     48000.0, noScale, false);
        check (r.valid, "F#1 808: stable note found");
        checkNear (r.detectedMidi, 30.0, 0.12, "F#1 808: pitch");
        check (! r.pitchEnvelope, "F#1 808: no envelope flagged");
        check (std::abs (r.deviationCents) < 10.0f, "F#1 808: near-zero deviation ("
                 + juce::String (r.deviationCents, 1) + " cents)");
    }

    {
        // +30 cents sharp G2: needle reads the deviation, the correction
        // brings it to the nearest semitone.
        const auto kick = render808 (43.0f, 1.2, 48000.0, 30.0f);
        const auto r = SamplePitchDetector::analyse (kick.data(), (int) kick.size(),
                                                     48000.0, noScale, false);
        check (r.valid, "sharp G2: analysed");
        checkNear (r.deviationCents, 30.0, 9.0, "sharp G2: deviation measured");
        checkNear (r.totalShiftSemitones, -0.30, 0.09, "sharp G2: correction is -30 cents");
        check (r.recommendedSemitones == 0, "sharp G2: no semitone jump needed");
    }

    {
        // G2 sample against an F# minor beat: nearest scale tone is F#2,
        // one semitone down - the mockup's own scenario.
        std::array<bool, 12> fsharpMinorScale { false, true, true, false, true, false,
                                                true,  false, true, true,  false, true };
        const auto kick = render808 (43.0f, 1.2, 48000.0);   // G2
        const auto r = SamplePitchDetector::analyse (kick.data(), (int) kick.size(),
                                                     48000.0, fsharpMinorScale, true);
        check (r.valid && r.recommendedSemitones == -1,
               "G2 vs F# minor: recommends -1 semitone (got "
                 + juce::String (r.recommendedSemitones) + ")");
    }

    {
        // A sample that glides an octave is a pitch envelope, not one note.
        const auto glide = render808 (36.0f, 1.2, 48000.0, 0.0f, 7.0f);
        const auto r = SamplePitchDetector::analyse (glide.data(), (int) glide.size(),
                                                     48000.0, noScale, false);
        check (! r.valid || r.pitchEnvelope,
               "gliding sample: flagged as a pitch envelope");
    }

    {
        // A noise burst has no stable note - honesty gate.
        juce::Random rng (0x808);
        std::vector<float> burst ((size_t) (48000.0 * 0.8), 0.0f);
        for (size_t i = 0; i < burst.size(); ++i)
            burst[i] = 0.6f * (rng.nextFloat() * 2.0f - 1.0f)
                        * std::exp (-(float) i / 9600.0f);
        const auto r = SamplePitchDetector::analyse (burst.data(), (int) burst.size(),
                                                     48000.0, noScale, false);
        check (! r.valid, "noise burst: no stable note claimed");
    }

    // --- preview pitch shifter ----------------------------------------------
    {
        PreviewPitchShifter shifter;
        shifter.prepare (48000.0, 512, 2);
        check (shifter.latencySamples() > 0, "shifter reports its dry-path latency");

        // Latency truth: an impulse through the DISENGAGED path (wet 0) must
        // arrive exactly at the reported latency.
        juce::AudioBuffer<float> impulse (2, 4096);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        shifter.process (impulse, 1.0f, 0.0f);
        int arrival = -1;
        for (int i = 0; i < 4096; ++i)
            if (std::abs (impulse.getSample (0, i)) > 0.5f)
            {
                arrival = i;
                break;
            }
        check (arrival == shifter.latencySamples(),
               "impulse arrives at the reported latency (got " + juce::String (arrival)
                 + ", reported " + juce::String (shifter.latencySamples()) + ")");
    }

    {
        // +12 semitones on a 220 Hz sine must come out dominated by 440 Hz,
        // at comparable loudness. Measured with a coarse DFT probe.
        PreviewPitchShifter shifter;
        shifter.prepare (48000.0, 512, 1);

        const int n = 48000;
        juce::AudioBuffer<float> audio (1, n);
        for (int i = 0; i < n; ++i)
            audio.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                              * 220.0 * i / 48000.0));
        const float inRms = audio.getRMSLevel (0, 0, n);

        for (int start = 0; start < n; start += 512)
        {
            juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 1, start,
                                           juce::jmin (512, n - start));
            shifter.process (view, 2.0f, 1.0f);
        }

        auto probe = [&audio, n] (double hz)
        {
            double re = 0.0, im = 0.0;
            for (int i = n / 2; i < n; ++i)   // steady-state half
            {
                const double w = juce::MathConstants<double>::twoPi * hz * i / 48000.0;
                re += audio.getSample (0, i) * std::cos (w);
                im += audio.getSample (0, i) * std::sin (w);
            }
            return std::sqrt (re * re + im * im);
        };

        const double at440 = probe (440.0), at220 = probe (220.0);
        check (at440 > at220 * 3.0,
               "+12 st shifts 220 Hz to 440 Hz (440:220 energy "
                 + juce::String (at440 / juce::jmax (1.0, at220), 1) + ":1)");

        const float outRms = audio.getRMSLevel (0, n / 2, n / 2);
        check (std::abs (juce::Decibels::gainToDecibels (outRms / inRms)) < 2.5f,
               "shifter is loudness-neutral within 2.5 dB (delta "
                 + juce::String (juce::Decibels::gainToDecibels (outRms / inRms), 2) + " dB)");
    }

    {
        // Engaging the preview mid-stream must not click.
        PreviewPitchShifter shifter;
        shifter.prepare (48000.0, 512, 1);
        juce::AudioBuffer<float> audio (1, 512);
        float previous = 0.0f, maxStep = 0.0f;
        double phase = 0.0;

        for (int block = 0; block < 60; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                phase += juce::MathConstants<double>::twoPi * 180.0 / 48000.0;
                audio.setSample (0, i, 0.4f * (float) std::sin (phase));
            }
            const float wet = block >= 30 ? 1.0f : 0.0f;   // hard toggle
            shifter.process (audio, 0.891f, wet);          // -2 st

            for (int i = 0; i < 512; ++i)
            {
                const float v = audio.getSample (0, i);
                if (block > 0 || i > 0)
                    maxStep = juce::jmax (maxStep, std::abs (v - previous));
                previous = v;
            }
        }
        check (maxStep < 0.12f,
               "engaging the preview is click-free (max step "
                 + juce::String (maxStep, 3) + ")");
    }

    // --- the money test: Apply Tune output is actually in tune --------------
    {
        // A G2 sample 30 cents sharp, dropped and tuned against F# minor:
        // the rendered file must analyse as F#2 within a few cents.
        const auto kick = render808 (43.0f, 1.2, 48000.0, 30.0f);
        auto sampleFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("keyglo-test-808.wav");
        sampleFile.deleteFile();
        {
            juce::WavAudioFormat wav;
            auto stream = sampleFile.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), 48000.0, 1, 24, {}, 0));
            check (writer != nullptr, "808 test wav written");
            if (writer != nullptr)
            {
                stream.release();
                juce::AudioBuffer<float> b (1, (int) kick.size());
                for (int i = 0; i < (int) kick.size(); ++i)
                    b.setSample (0, i, kick[(size_t) i]);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }

        KeyGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        // Give it the F# minor beat context first (via the beat file path).
        {
            const auto beat = renderLoop (fsharpMinor, 46.25f, 9.0, 44100.0, 0.0);

            auto beatFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("keyglo-test-beat-m4.wav");
            beatFile.deleteFile();
            {
                // The writer MUST be destroyed before the worker reads the
                // file: WAV headers are finalised in the destructor, and an
                // unflushed file reads as ~empty. This exact bug shipped in
                // the first version of this test.
                juce::WavAudioFormat wav;
                auto stream = beatFile.createOutputStream();
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    wav.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
                if (writer != nullptr)
                {
                    stream.release();
                    juce::AudioBuffer<float> b (1, (int) beat.size());
                    for (int i = 0; i < (int) beat.size(); ++i)
                        b.setSample (0, i, beat[(size_t) i]);
                    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
            }
            p.analyseFileAsync (beatFile);
            for (int tries = 0; tries < 300; ++tries)
            {
                juce::Thread::sleep (50);
                auto s = p.getDisplayModel().get();
                if (s->hasBeatResult && ! s->analyzing)
                    break;
            }
            beatFile.deleteFile();
        }
        {
            // NOT just key=="F#": the default snapshot's key is also "F#",
            // so that alone passes with the analysis silently failed.
            auto s = p.getDisplayModel().get();
            check (s->hasBeatResult && ! s->noReliableKey
                     && s->key == "F#" && s->scale == "Minor",
                   "beat context is a RELIABLE F# minor (got "
                     + juce::String (s->hasBeatResult ? (s->noReliableKey ? "unreliable" : "ok")
                                                      : "none")
                     + ", " + s->key + " " + s->scale + ")");
            check (p.getDisplayModel().previewArmed(),
                   "preview shifter arms once the beat result stands");
        }

        p.analyseSampleAsync (sampleFile);
        for (int tries = 0; tries < 300; ++tries)
        {
            juce::Thread::sleep (50);
            if (p.getDisplayModel().get()->hasSampleResult)
                break;
        }
        auto snap = p.getDisplayModel().get();
        check (snap->hasSampleResult && ! snap->sampleNoStableNote,
               "dropped 808 analysed");
        check (snap->sampleNote == "G2", "dropped 808 detected as G2 (got "
                 + snap->sampleNote + ")");
        // The sample is G2 +30 cents: the nearest F#-minor tone is G#2 at
        // +0.70 st, closer than F#2 at -1.30 (the in-tune-G2 -> F#2 case is
        // covered by the direct detector test above).
        check (snap->sampleRecommendedSemitones == 1,
               "sharp G2 recommends +1 st toward G# (got "
                 + juce::String (snap->sampleRecommendedSemitones) + ")");
        checkNear (snap->sampleFineTuneCents, -30.0, 9.0,
                   "sharp G2 fine remainder is -30 cents");
        check (snap->sampleTunedReady, "tuned audition buffer is ready for SOLO");

        // Apply Tune, then re-analyse the rendered file with no key context:
        // it must land on F#2 within a few cents.
        p.applyTuneAsync();
        juce::String renderedPath;
        for (int tries = 0; tries < 300; ++tries)
        {
            juce::Thread::sleep (50);
            renderedPath = p.getDisplayModel().get()->appliedTunePath;
            if (renderedPath.isNotEmpty())
                break;
        }
        check (renderedPath.isNotEmpty(), "Apply Tune rendered a file");

        if (renderedPath.isNotEmpty())
        {
            juce::File rendered (renderedPath);
            check (rendered.existsAsFile(), "rendered file exists on disk");

            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (rendered));
            check (reader != nullptr, "rendered file is readable");
            if (reader != nullptr)
            {
                std::vector<float> tuned ((size_t) reader->lengthInSamples, 0.0f);
                juce::AudioBuffer<float> b (1, (int) reader->lengthInSamples);
                reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, false);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    tuned[(size_t) i] = b.getSample (0, i);

                const auto verify = SamplePitchDetector::analyse (tuned.data(),
                                                                  (int) tuned.size(),
                                                                  reader->sampleRate,
                                                                  noScale, false);
                check (verify.valid, "rendered file has a stable note");
                checkNear (verify.detectedMidi, 44.0, 0.08,
                           "rendered file IS G#2 in tune");   // G2+30c shifted +0.70 st
            }
            rendered.deleteFile();
        }
        sampleFile.deleteFile();

        // SOLO actually plays the tuned sample through the output.
        {
            setParam (p, pid::sampleSolo, 1.0f);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            for (int i = 0; i < 40; ++i)
            {
                block.clear();                        // silent input
                p.processBlock (block, midi);
                peak = juce::jmax (peak, block.getMagnitude (0, 0, 512));
            }
            check (peak > 0.05f, "SOLO plays the tuned sample over silence (peak "
                     + juce::String (peak, 3) + ")");

            setParam (p, pid::sampleSolo, 0.0f);
            for (int i = 0; i < 20; ++i)
            {
                block.clear();
                p.processBlock (block, midi);
            }
            block.clear();
            p.processBlock (block, midi);
            check (block.getMagnitude (0, 0, 512) < 0.01f,
                   "SOLO off returns to the program path");
        }
    }

    // =======================================================================
    //  Product milestone - presets, undo, MIDI scale export
    // =======================================================================

    // Sandbox the user-preset folder for every preset test.
    auto presetSandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("KeyGloTestPresets");
    presetSandbox.deleteRecursively();
    PresetManager::dirOverride() = presetSandbox;

    {
        KeyGloProcessor p;
        auto& bank = p.getPresets();

        check ((int) bank.all().size() == PresetManager::factoryCount,
               "six factory presets, no user presets yet");
        check (bank.currentName() == "Male Rap Hook Match",
               "fresh instance sits on preset 0");
        check (! bank.isModified(), "fresh instance is unmodified");

        // Preset 0 must equal the parameter defaults - a fresh instance and
        // 'load preset 0' are the same state.
        for (const auto& id : PresetManager::creativeParams())
        {
            auto* param = p.getAPVTS().getParameter (id);
            checkNear (param->getValue(), param->getDefaultValue(), 1.0e-4,
                       "preset 0 matches default: " + id);
        }

        // Stepping applies values and wraps.
        bank.step (1);
        check (bank.currentName() == "Female R&B Range", "step forward");
        checkNear (p.getAPVTS().getRawParameterValue (pid::rangeSense)->load(),
                   0.80, 1.0e-3, "preset 2 applied rangeSense");
        bank.step (-2);
        check (bank.currentName() == "Producer Quick Check", "step wraps backward");

        // Loading a preset never touches the excluded parameters.
        setParam (p, pid::outputGainDb, -10.0f);
        setParam (p, pid::pluginBypass, 1.0f);
        bank.select (0);
        checkNear (p.getAPVTS().getRawParameterValue (pid::outputGainDb)->load(),
                   -10.0, 1.0e-3, "preset load leaves the output trim alone");
        check (p.getAPVTS().getRawParameterValue (pid::pluginBypass)->load() > 0.5f,
               "preset load leaves bypass alone");

        // Modified tracking: tweak a covered knob -> modified; back -> clean.
        setParam (p, pid::keySense, 0.5f);
        check (bank.isModified(), "tweaking a covered param marks modified");
        setParam (p, pid::keySense, 0.85f);
        check (! bank.isModified(), "restoring the value clears modified");

        // User preset save + reload.
        setParam (p, pid::keySense, 0.33f);
        check (bank.saveCurrentAs ("Test Vibe"), "user preset saves");
        check (bank.currentName() == "Test Vibe" && ! bank.currentIsFactory(),
               "save selects the new user preset");
        check (! bank.isModified(), "freshly saved preset is unmodified");

        bank.select (0);
        checkNear (p.getAPVTS().getRawParameterValue (pid::keySense)->load(),
                   0.85, 1.0e-3, "factory preset restores keySense");
        bank.restoreByName ("Test Vibe");
        bank.select (bank.getCurrentIndex());
        checkNear (p.getAPVTS().getRawParameterValue (pid::keySense)->load(),
                   0.33, 1.0e-3, "user preset round-trips its values");

        // Awkward names stay inside the folder.
        check (PresetManager::fileFor ("../../evil").getParentDirectory()
                 == PresetManager::userDir(),
               "preset names cannot escape the presets directory");
    }

    {
        // Session restore: the preset name survives the host state, and a
        // modified preset comes back modified.
        KeyGloProcessor a;
        a.getPresets().rebuildList();
        a.getPresets().restoreByName ("Test Vibe");
        a.getPresets().select (a.getPresets().getCurrentIndex());
        setParam (a, pid::rangeSense, 0.11f);   // modify it

        juce::MemoryBlock state;
        a.getStateInformation (state);

        KeyGloProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        check (b.getPresets().currentName() == "Test Vibe",
               "preset name restores with the session");
        check (b.getPresets().isModified(),
               "a modified preset restores as modified");
        checkNear (b.getAPVTS().getRawParameterValue (pid::rangeSense)->load(),
                   0.11, 1.0e-3, "the modified value itself restores");
        check (! b.getUndoManager().canUndo(),
               "session restore is not an undoable step");
    }

    {
        // Undo/redo across parameter changes and preset loads.
        KeyGloProcessor p;
        auto& um = p.getUndoManager();

        // captureUndoPoint() is what the UI timer calls: parameter moves are
        // banked into one step per interval.
        setParam (p, pid::keySense, 0.2f);
        p.getPresets().captureUndoPoint();
        setParam (p, pid::rangeSense, 0.9f);
        p.getPresets().captureUndoPoint();

        check (um.canUndo(), "changes are undoable");
        um.undo();
        p.getPresets().resyncUndoBaseline();
        checkNear (p.getAPVTS().getRawParameterValue (pid::rangeSense)->load(),
                   0.72, 1.0e-3, "undo restores the last change");
        checkNear (p.getAPVTS().getRawParameterValue (pid::keySense)->load(),
                   0.2, 1.0e-3, "undo leaves the earlier change");
        um.redo();
        p.getPresets().resyncUndoBaseline();
        checkNear (p.getAPVTS().getRawParameterValue (pid::rangeSense)->load(),
                   0.9, 1.0e-3, "redo re-applies it");

        // Several moves inside one interval collapse to a single step.
        setParam (p, pid::previewMix, 0.1f);
        setParam (p, pid::previewMix, 0.2f);
        setParam (p, pid::previewMix, 0.3f);
        p.getPresets().captureUndoPoint();
        um.undo();
        p.getPresets().resyncUndoBaseline();
        checkNear (p.getAPVTS().getRawParameterValue (pid::previewMix)->load(),
                   0.4, 1.0e-3, "a drag inside one interval is ONE undo step");

        p.getPresets().select (4);   // 808 Tune Focus
        checkNear (p.getAPVTS().getRawParameterValue (pid::keySense)->load(),
                   0.95, 1.0e-3, "preset applied");
        um.undo();
        p.getPresets().resyncUndoBaseline();
        checkNear (p.getAPVTS().getRawParameterValue (pid::keySense)->load(),
                   0.2, 1.0e-3, "a preset load is one undoable step");
    }

    // --- user data lives in ONE place, correct for the platform -------------
    {
        // Presets and profiles must sit under the same KeyGlo folder. They
        // once did not: profiles landed in ~/Library/Diamond Loopz while
        // presets used ~/Library/Application Support/Diamond Loopz, and the
        // shipped Read Me documented only the second. Nothing failed - the
        // data was just somewhere the user was never told about.
        PresetManager::dirOverride() = juce::File();
        ArtistProfileStore::dirOverride() = juce::File();

        const auto root = AppPaths::dataDirectory();
        check (PresetManager::userDir().isAChildOf (root),
               "presets live under the KeyGlo data folder");
        check (ArtistProfileStore::directory().isAChildOf (root),
               "profiles live under the SAME KeyGlo data folder");
        check (root.getFileName() == "KeyGlo"
                 && root.getParentDirectory().getFileName() == "Diamond Loopz",
               "data folder is .../Diamond Loopz/KeyGlo (got "
                 + root.getFullPathName() + ")");

       #if JUCE_MAC
        check (root.getFullPathName().contains ("Application Support"),
               "macOS: inside Application Support");
       #else
        check (! root.getFullPathName().contains ("Application Support"),
               "non-macOS: no bogus 'Application Support' segment");
       #endif

        // Restore the sandbox for the remaining preset tests.
        PresetManager::dirOverride() = presetSandbox;
    }

    // --- MIDI scale export --------------------------------------------------
    {
        juce::StringArray fsMinor { "F#", "G#", "A", "B", "C#", "D", "E" };
        auto file = AutoTuneSetupPanel::writeScaleMidiFile ("F#", "Minor", fsMinor);
        check (file.existsAsFile(), "scale MIDI file written");

        juce::FileInputStream in (file);
        juce::MidiFile midi;
        check (in.openedOk() && midi.readFrom (in), "scale MIDI file parses");
        midi.convertTimestampTicksToSeconds();

        std::vector<int> played;
        if (midi.getNumTracks() > 0)
        {
            const auto* track = midi.getTrack (0);
            for (int i = 0; i < track->getNumEvents(); ++i)
                if (track->getEventPointer (i)->message.isNoteOn())
                    played.push_back (track->getEventPointer (i)->message.getNoteNumber());
        }
        check ((int) played.size() == 8, "one octave: 8 notes (7 + top root), got "
                 + juce::String ((int) played.size()));
        if (played.size() == 8)
        {
            check (played.front() % 12 == 6 && played.back() % 12 == 6,
                   "starts and ends on the root");
            check (played.back() - played.front() == 12, "spans exactly one octave");
            bool ascending = true, inScale = true;
            const bool fsMinorPcs[12] = { false, true, true, false, true, false,
                                          true,  false, true, true,  false, true };
            for (size_t i = 0; i < played.size(); ++i)
            {
                if (i > 0 && played[i] <= played[i - 1])
                    ascending = false;
                inScale = inScale && fsMinorPcs[played[i] % 12];
            }
            check (ascending, "notes ascend");
            check (inScale, "every note is in F# minor");
        }
        file.deleteFile();
    }

    presetSandbox.deleteRecursively();
    PresetManager::dirOverride() = juce::File();

    std::printf ("%d checks, %d failed\n", checksRun, checksFailed);
    return checksFailed == 0 ? 0 : 1;
}
