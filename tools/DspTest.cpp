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

    std::printf ("%d checks, %d failed\n", checksRun, checksFailed);
    return checksFailed == 0 ? 0 : 1;
}
