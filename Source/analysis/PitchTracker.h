/*
    PitchTracker - monophonic pitch detection (YIN with cumulative mean
    normalized difference, parabolic lag interpolation).

    Input is decimated to ~16 kHz first: vocal fundamentals live below
    1 kHz, and the smaller frames keep the worker cheap enough to run at
    trail rate. Unvoiced/noisy/breath frames are rejected by the CMNDF
    minimum and an energy floor - a gap in the trail, never a false note
    (DSP_AND_ANALYSIS_ARCHITECTURE.md).
*/

#pragma once
#include <JuceHeader.h>
#include <vector>

namespace keyglo
{

struct PitchFrame
{
    float hz = 0.0f;
    float midi = 0.0f;         // valid only when voiced
    float cents = 0.0f;        // deviation from the nearest semitone
    float confidence = 0.0f;   // 1 - CMNDF minimum
    bool voiced = false;
};

class PitchTracker
{
public:
    static constexpr int frameSize = 1024;    // at the decimated rate (~64 ms)
    static constexpr double targetRate = 16000.0;
    static constexpr float minHz = 60.0f, maxHz = 1000.0f;

    // One frame at the DECIMATED rate (n >= frameSize samples at decimatedRate).
    static PitchFrame trackFrame (const float* x, int n, double decimatedRate);

    // Whole-buffer analysis at hopSeconds intervals; input at any rate,
    // decimation handled internally. Used for hook statistics and tests.
    static std::vector<PitchFrame> analyseBuffer (const float* mono, int numSamples,
                                                  double sampleRate,
                                                  double hopSeconds = 0.02);

    // Boxcar decimation by an integer factor (fine for pitch - fundamentals
    // sit far below the folded band).
    static void decimate (const float* in, int numIn, int factor,
                          std::vector<float>& out);

    static int decimationFactor (double sampleRate)
    {
        return juce::jmax (1, (int) std::round (sampleRate / targetRate));
    }
};

} // namespace keyglo
