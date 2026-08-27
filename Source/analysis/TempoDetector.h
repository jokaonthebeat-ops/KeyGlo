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

    // Diagnostics for ground-truth work (make analyse): the raw
    // autocorrelation curve and the rate its lags are expressed in, so a
    // caller can ask what the envelope says at any specific BPM.
    std::vector<float> acCurve;
    double fluxRate = 0.0;
    float acAtBpm (double bpm) const
    {
        if (fluxRate <= 0.0 || bpm <= 0.0) return 0.0f;
        const int lag = (int) std::lround (60.0 * fluxRate / bpm);
        return (lag >= 0 && lag < (int) acCurve.size()) ? acCurve[(size_t) lag] : 0.0f;
    }
};

class TempoDetector
{
public:
    static TempoResult analyse (const float* mono, int numSamples, double sampleRate);
};

} // namespace keyglo
