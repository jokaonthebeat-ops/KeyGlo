// -----------------------------------------------------------------------------
//  Ground-truth checker: runs the real detectors over audio files and prints
//  what they return. Built for iterating on the analysis engines against REAL
//  music, where the filename often carries the truth:
//
//    make analyse ARGS="'beat_BPM132_F#min.wav' other.wav"
//
//  Synthetic fixtures cannot produce the errors real productions do - the
//  132 BPM beat KeyGlo first read as 88 (a 2:3 metrical error from triplet
//  hats) never appeared in a straight click track.
// -----------------------------------------------------------------------------

#include "../Source/analysis/BeatKeyDetector.h"
#include "../Source/analysis/TempoDetector.h"
#include <cstdio>

using namespace keyglo;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (int i = 1; i < argc; ++i)
    {
        const juce::File f { juce::String (argv[i]) };
        if (! f.existsAsFile())
        {
            std::printf ("no such file: %s\n", argv[i]);
            continue;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
        {
            std::printf ("unreadable: %s\n", argv[i]);
            continue;
        }

        const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                     (juce::int64) (reader->sampleRate * 30.0));
        juce::AudioBuffer<float> buf ((int) reader->numChannels, n);
        reader->read (&buf, 0, n, 0, true, true);

        std::vector<float> mono ((size_t) n, 0.0f);
        for (int ch = 0; ch < (int) reader->numChannels; ++ch)
            for (int s = 0; s < n; ++s)
                mono[(size_t) s] += buf.getSample (ch, s) / (float) reader->numChannels;

        const auto key = BeatKeyDetector::analyse (mono.data(), n, reader->sampleRate);
        const auto tempo = TempoDetector::analyse (mono.data(), n, reader->sampleRate);

        std::printf ("\n%s\n", f.getFileName().toRawUTF8());
        std::printf ("  key   : %s %s   (reliable %d, conf %.2f, tuning %+.0f cents)\n",
                     key.keyName().toRawUTF8(), key.scaleName().toRawUTF8(),
                     (int) key.reliable, key.confidence, key.tuningCents);
        if (! key.alternatives.empty())
        {
            BeatKeyResult alt; alt.rootPc = key.alternatives[0].rootPc;
            std::printf ("  alt   : %s %s\n", alt.keyName().toRawUTF8(),
                         key.alternatives[0].minor ? "Minor" : "Major");
        }
        std::printf ("  gates : peakShare %.3f  inScale %.2f  bestR %.2f\n",
                     key.gatePeakShare, key.gateSpread, key.gateBestR);
        std::printf ("  tempo : %.1f BPM  (reliable %d, conf %.2f)\n",
                     tempo.bpm, (int) tempo.reliable, tempo.confidence);
        std::printf ("  cands : ");
        for (float c : tempo.candidates)
            std::printf ("%.1f  ", c);
        std::printf ("\n");

        // What does the envelope actually say at the musically plausible
        // levels? If the true tempo has no peak here, no amount of ranking
        // will find it and the honest answer is to report uncertainty.
        std::printf ("  probe : ");
        for (double b : { 66.0, 84.0, 88.0, 105.0, 126.0, 132.0, 168.0, 176.0 })
            std::printf ("%.0f=%.3f  ", b, tempo.acAtBpm (b));
        std::printf ("\n");
    }
    return 0;
}
