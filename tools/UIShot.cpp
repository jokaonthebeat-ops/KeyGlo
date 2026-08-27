// -----------------------------------------------------------------------------
//  Renders the KeyGlo editor to a PNG without opening a window.
//
//    make uishot                            -> build/KeyGlo-ui.png at 1491x1055
//    make uishot ARGS="out.png min"         -> 1044x739
//    make uishot ARGS="out.png max"         -> 2237x1583
//    make uishot ARGS="out.png def signal"  -> feed test audio first so the
//                                              meters + analysis run live
//    make uishot ARGS="out.png def demo"    -> demo display mode: the
//                                              contract dataset + DemoFeed
//                                              (approved-reference overlay,
//                                              marketing shots)
//    make uishot ARGS="out.png def signal settle=N" -> N refresh frames
//                                              (animation phase control)
//
//  Reports artwork that failed to load - the difference between "the design
//  is wrong" and "the install is wrong".
// -----------------------------------------------------------------------------

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>

using namespace keyglo;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outName = argc > 1 ? argv[1] : "KeyGlo-ui.png";
    const juce::String sizeArg = argc > 2 ? juce::String (argv[2]).toLowerCase() : "def";
    const juce::String modeArg = argc > 3 ? juce::String (argv[3]).toLowerCase() : "";

    int settleFrames = 90;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("settle="))
            settleFrames = juce::jlimit (1, 2000, a.fromFirstOccurrenceOf ("=", false, false)
                                                     .getIntValue());
    }

    int width = Design::width, height = Design::height;
    if (sizeArg == "min")      { width = Design::minWidth; height = Design::minHeight; }
    else if (sizeArg == "max") { width = Design::maxWidth; height = Design::maxHeight; }
    else if (sizeArg.containsChar ('x'))
    {
        width  = sizeArg.upToFirstOccurrenceOf ("x", false, false).getIntValue();
        height = sizeArg.fromFirstOccurrenceOf ("x", false, false).getIntValue();
    }

    KeyGloProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // "vocal" mode: give the engine a profile and sing a phrase, so the
    // shot shows the artist side alive (trail, note readout, fit pods).
    if (modeArg.contains ("vocal"))
    {
        ArtistProfile profile;
        profile.extendedLowMidi = 48; profile.comfortableLowMidi = 52;
        profile.strongLowMidi = 55;   profile.strongHighMidi = 64;
        profile.comfortableHighMidi = 67; profile.extendedHighMidi = 71;
        profile.falsettoHighMidi = 71;
        processor.getVocalEngine().setProfile (profile);
    }

    if (modeArg.contains ("demo"))
    {
        // The approved-reference state: contract dataset presented as a
        // result, ambience from the DemoFeed.
        demoDisplayMode() = true;
        auto demoSnap = std::make_shared<AnalysisSnapshot>();
        demoSnap->hasBeatResult = true;
        demoSnap->sourceName = "DEMO";
        processor.getDisplayModel().publish (std::move (demoSnap));
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::printf ("FAIL: createEditor returned null\n");
        return 2;
    }

    editor->setSize (width, height);
    auto* kgEditor = dynamic_cast<KeyGloEditor*> (editor.get());
    if (kgEditor == nullptr)
    {
        std::printf ("FAIL: editor is not a KeyGloEditor\n");
        return 2;
    }

    // F#-minor-flavoured test feed at 148 BPM: an 808-ish bass line walking
    // i-VI-iv-VII with kick pulses and a filtered noise bed. Enough musical
    // truth for the real engine to detect key and tempo from the ring.
    auto feedSignal = [&] (double seconds)
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        juce::Random random (0x4b47);
        double bassPhase = 0.0, chordPhase[3] = { 0.0, 0.0, 0.0 };
        float lp = 0.0f;
        static int sampleIndex = 0;

        static const double bassHz[4] = { 46.25, 36.71, 61.74, 41.20 };  // F#1 D1 B1 E1
        static const double triads[4][3] = { { 185.0, 220.0, 277.18 },   // F#m
                                             { 146.83, 185.0, 220.0 },   // D
                                             { 123.47, 146.83, 185.0 },  // Bm
                                             { 164.81, 207.65, 246.94 } };// E

        const int blocks = (int) (48000.0 * seconds / 512.0);
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (double) sampleIndex / 48000.0;
                const double beatLen = 60.0 / 148.0;
                const int bar = (int) (t / (beatLen * 4.0));
                const int chord = bar % 4;
                const double beatPos = std::fmod (t, beatLen);
                const float kick = (float) std::exp (-beatPos * 9.0);

                bassPhase += 2.0 * juce::MathConstants<double>::pi
                               * (bassHz[chord] + 7.0 * kick) / 48000.0;
                float v = 0.48f * (0.45f + 0.55f * kick) * (float) std::sin (bassPhase);

                for (int n = 0; n < 3; ++n)
                {
                    chordPhase[n] += 2.0 * juce::MathConstants<double>::pi
                                       * triads[chord][n] / 48000.0;
                    v += 0.16f * (float) std::sin (chordPhase[n])
                       + 0.05f * (float) std::sin (2.0 * chordPhase[n]);
                }

                const float white = random.nextFloat() * 2.0f - 1.0f;
                lp += 0.12f * (white - lp);
                v += 0.035f * lp;

                audio.setSample (0, i, v);
                audio.setSample (1, i, v * 0.96f);
                ++sampleIndex;
            }
            processor.processBlock (audio, midi);
        }
    };

    // A sung phrase in F# minor over the beat: harmonic-rich, vibrato,
    // so the pitch tracker has real material.
    auto feedVoice = [&]
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        const int melody[] = { 61, 63, 64, 66, 64, 63, 61, 59, 61, 64 };
        double phase[6] = { 0, 0, 0, 0, 0, 0 };
        const double amps[6] = { 1.0, 0.55, 0.32, 0.18, 0.10, 0.06 };

        int written = 0, blocksSinceSleep = 0;
        for (int n = 0; n < 10; ++n)
        {
            const double f0 = 440.0 * std::pow (2.0, (melody[n] - 69) / 12.0);
            const int noteSamples = (int) (48000.0 * 0.75);
            for (int i = 0; i < noteSamples; ++i)
            {
                const double t = i / 48000.0;
                const double vib = std::pow (2.0, (22.0 * std::sin (juce::MathConstants<double>::twoPi
                                                                      * 5.2 * t)) / 1200.0);
                const float env = (float) (juce::jmin (1.0, t / 0.05)
                                            * juce::jmin (1.0, (0.75 - t) / 0.05));
                float v = 0.0f;
                for (int h = 0; h < 6; ++h)
                {
                    phase[h] += juce::MathConstants<double>::twoPi * f0 * vib * (h + 1) / 48000.0;
                    v += (float) (amps[h] * std::sin (phase[h]));
                }
                audio.setSample (0, written, 0.22f * env * v);
                audio.setSample (1, written, 0.22f * env * v * 0.97f);
                if (++written == 512)
                {
                    processor.processBlock (audio, midi);
                    written = 0;
                    // Real-time pacing: the vocal worker caps how much
                    // backlog it pitch-tracks per pass, so dumping the whole
                    // phrase at once would leave most of it untracked.
                    if (++blocksSinceSleep >= 1)
                    {
                        juce::Thread::sleep (11);   // ~= 512 samples at 48 kHz
                        blocksSinceSleep = 0;
                    }
                }
            }
        }
    };

    if (modeArg.contains ("vocal"))
    {
        feedVoice();
        for (int tries = 0; tries < 200; ++tries)
        {
            juce::Thread::sleep (25);
            if (processor.getDisplayModel().get()->hasFitResult)
                break;
        }
    }

    if (modeArg.contains ("signal"))
    {
        // In demo mode the feed only lights the meters/spectrum - the demo
        // snapshot stays authoritative, so the real engine's verdict on the
        // test loop must not replace it.
        feedSignal (demoDisplayMode() ? 1.0 : 8.5);
        if (! demoDisplayMode())
        {
            processor.analyseCaptureNow();
            for (int tries = 0; tries < 300; ++tries)
            {
                auto snap = processor.getDisplayModel().get();
                if (snap->hasBeatResult && ! snap->analyzing)
                    break;
                juce::Thread::sleep (50);
            }
        }
    }

    // Let the animated displays settle (pods tween in, trail scrolls,
    // spectrum smooths). Deterministic frame count = reproducible shots.
    for (int i = 0; i < settleFrames; ++i)
        kgEditor->refreshDisplays();

    // Top the meters back up so the capture shows them lit.
    if (modeArg.contains ("signal"))
    {
        feedSignal (0.3);
        juce::Thread::sleep (120);   // let the fast lane publish the spectrum
        kgEditor->refreshDisplays();
    }

    juce::Image image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (outName);
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, *stream))
        {
            std::printf ("FAIL: could not encode %s\n", outName.toRawUTF8());
            return 2;
        }
    }
    else
    {
        std::printf ("FAIL: could not open %s for writing\n", outName.toRawUTF8());
        return 2;
    }

    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), width, height);

    if (Assets::loadFailureCount() > 0)
    {
        std::printf ("\nWARNING: %d asset(s) failed to load:\n%s\n",
                     Assets::loadFailureCount(), Assets::describeFailures().toRawUTF8());
        return 1;
    }

    std::printf ("all artwork loaded from %s\n",
                 Assets::assetsDirectory().getFullPathName().toRawUTF8());
    return 0;
}
