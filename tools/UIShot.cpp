// -----------------------------------------------------------------------------
//  Renders the KeyGlo editor to a PNG without opening a window.
//
//    make uishot                            -> build/KeyGlo-ui.png at 1491x1055
//    make uishot ARGS="out.png min"         -> 1044x739
//    make uishot ARGS="out.png max"         -> 2237x1583
//    make uishot ARGS="out.png def signal"  -> feed test audio first so the
//                                              meters run from real levels
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

    auto feedSignal = [&]
    {
        // 808-flavoured test feed at the demo BPM: 46 Hz tone bursts plus a
        // filtered noise bed - honest levels for the meters.
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        juce::Random random (0x4b47);
        double phase = 0.0;
        float lp = 0.0f;
        static int sampleIndex = 0;

        for (int block = 0; block < 24; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double beatPos = std::fmod ((double) sampleIndex / 48000.0, 0.405); // 148 BPM
                const float env = (float) std::exp (-beatPos * 8.0);

                phase += 2.0 * juce::MathConstants<double>::pi * (46.25 + 20.0 * env) / 48000.0;
                const float bass = 0.8f * env * (float) std::sin (phase);

                const float white = random.nextFloat() * 2.0f - 1.0f;
                lp += 0.12f * (white - lp);
                const float bed = 0.09f * lp;

                audio.setSample (0, i, bass + bed);
                audio.setSample (1, i, bass + bed * 0.9f);
                ++sampleIndex;
            }
            processor.processBlock (audio, midi);
        }
    };

    if (modeArg.contains ("signal"))
        feedSignal();

    // Let the animated displays settle (pods tween in, trail scrolls,
    // spectrum smooths). Deterministic frame count = reproducible shots.
    for (int i = 0; i < settleFrames; ++i)
        kgEditor->refreshDisplays();

    // Top the meters back up so the capture shows them lit.
    if (modeArg.contains ("signal"))
    {
        feedSignal();
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
