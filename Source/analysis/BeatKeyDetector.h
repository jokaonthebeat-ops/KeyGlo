/*
    BeatKeyDetector - offline key/scale/tuning analysis of a mono buffer.

    Pipeline (DSP_AND_ANALYSIS_ARCHITECTURE.md):
      1. Long overlapping FFT frames (32768-point - chroma needs ~1.5 Hz bins
         so the bass register lands on the right semitone; a 4096-point FFT
         mis-bins everything below ~200 Hz, the SourceGlo lesson).
      2. Spectral peak picking with magnitude compression, so stable harmonic
         content outweighs transients and broadband noise.
      3. Tuning-offset estimation from the peaks' cents deviations, BEFORE
         chroma binning; the pitch-class mapping is then compensated.
      4. 12-bin chroma with the bass register (< 250 Hz) weighted 2.2x - the
         808/bass root IS the tonic in this catalogue, and it is what breaks
         relative-key ties (A major vs F# minor share every scale note).
      5. Krumhansl-Schmuckler major/minor template correlation over all 24
         candidates; confidence from fit and margin; ranked alternatives.

    Sparse percussion or noise gets `reliable = false` - show "No Reliable
    Key" rather than inventing one.
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

namespace keyglo
{

struct BeatKeyResult
{
    bool reliable = false;
    int rootPc = 0;               // 0 = C
    bool minor = false;
    float confidence = 0.0f;      // 0..1
    float tuningCents = 0.0f;     // global deviation from A440
    std::array<float, 12> chroma {};

    struct Candidate { int rootPc; bool minor; float confidence; };
    std::vector<Candidate> alternatives;   // ranked, best-first, excludes winner

    // Gate diagnostics (tuning/tests; not shown in the UI).
    float gatePeakShare = 0.0f, gateSpread = 0.0f, gateBestR = 0.0f;

    juce::String keyName() const
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };
        return names[((rootPc % 12) + 12) % 12];
    }
    juce::String scaleName() const  { return minor ? "Minor" : "Major"; }
};

class BeatKeyDetector
{
public:
    // sampleRate of `mono`; analyses up to the first ~30 s.
    static BeatKeyResult analyse (const float* mono, int numSamples, double sampleRate);
};

} // namespace keyglo
