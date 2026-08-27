/*
    TempoDetector - offline BPM estimation by onset-strength autocorrelation.

    Spectral-flux onset envelope (STFT 1024/512), moving-average detrended,
    autocorrelated over 55..220 BPM lags. Half/double-time candidates are
    reported when their evidence is comparable (DSP_AND_ANALYSIS_ARCHITECTURE:
    show the ambiguity instead of forcing one answer).
*/

#pragma once
#include <JuceHeader.h>
#include <vector>

namespace keyglo
{

struct TempoResult
{
    bool reliable = false;
    float bpm = 0.0f;
    float confidence = 0.0f;
    std::vector<float> candidates;   // ranked, best first (includes bpm)
};

class TempoDetector
{
public:
    static TempoResult analyse (const float* mono, int numSamples, double sampleRate);
};

} // namespace keyglo
