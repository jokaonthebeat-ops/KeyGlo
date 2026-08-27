#include "PitchTracker.h"

namespace keyglo
{

void PitchTracker::decimate (const float* in, int numIn, int factor,
                             std::vector<float>& out)
{
    const int numOut = numIn / juce::jmax (1, factor);
    out.resize ((size_t) numOut);
    for (int i = 0; i < numOut; ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < factor; ++k)
            sum += in[i * factor + k];
        out[(size_t) i] = sum / (float) factor;
    }
}

PitchFrame PitchTracker::trackFrame (const float* x, int n, double rate,
                                     float minFreq, float maxFreq)
{
    PitchFrame frame;
    if (x == nullptr || n < frameSize || rate <= 0.0)
        return frame;

    constexpr int w = frameSize / 2;                       // integration window
    const int tauMin = juce::jmax (2, (int) (rate / maxFreq));
    const int tauMax = juce::jmin (frameSize - w - 1, (int) (rate / minFreq));
    if (tauMax <= tauMin + 2)
        return frame;

    // Energy floor: breath/noise-only frames never become notes.
    double energy = 0.0;
    for (int i = 0; i < w; ++i)
        energy += (double) x[i] * x[i];
    const float rms = (float) std::sqrt (energy / w);
    if (rms < 2.0e-4f)
        return frame;

    // Difference function + cumulative mean normalisation (YIN).
    static thread_local std::vector<float> diff, cmndf;
    diff.assign ((size_t) tauMax + 1, 0.0f);
    cmndf.assign ((size_t) tauMax + 1, 1.0f);

    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int i = 0; i < w; ++i)
        {
            const float delta = x[i] - x[i + tau];
            d += (double) delta * delta;
        }
        diff[(size_t) tau] = (float) d;
    }

    double runningSum = 0.0;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        runningSum += diff[(size_t) tau];
        cmndf[(size_t) tau] = runningSum > 1.0e-12
                                ? diff[(size_t) tau] * (tau - tauMin + 1) / (float) runningSum
                                : 1.0f;
    }

    // First dip under the absolute threshold; fall back to the global
    // minimum. Then descend to the local minimum (the threshold crossing can
    // land on the dip's shoulder).
    constexpr float threshold = 0.15f;
    int tauEstimate = -1;
    for (int tau = tauMin + 1; tau < tauMax; ++tau)
    {
        if (cmndf[(size_t) tau] < threshold)
        {
            while (tau + 1 < tauMax && cmndf[(size_t) tau + 1] < cmndf[(size_t) tau])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }
    if (tauEstimate < 0)
    {
        float best = 1.0f;
        for (int tau = tauMin + 1; tau < tauMax; ++tau)
            if (cmndf[(size_t) tau] < best)
            {
                best = cmndf[(size_t) tau];
                tauEstimate = tau;
            }
    }
    if (tauEstimate < tauMin + 1 || tauEstimate >= tauMax)
        return frame;

    const float dipDepth = cmndf[(size_t) tauEstimate];
    frame.confidence = 1.0f - dipDepth;

    // Parabolic interpolation around the dip.
    double tauRefined = tauEstimate;
    {
        const float l = cmndf[(size_t) tauEstimate - 1];
        const float m = cmndf[(size_t) tauEstimate];
        const float r = cmndf[(size_t) tauEstimate + 1];
        const float denom = l - 2.0f * m + r;
        if (std::abs (denom) > 1.0e-9f)
            tauRefined += juce::jlimit (-0.5f, 0.5f, 0.5f * (l - r) / denom);
    }

    frame.hz = (float) (rate / tauRefined);
    frame.voiced = dipDepth < 0.30f && frame.hz >= minFreq && frame.hz <= maxFreq;
    if (frame.voiced)
    {
        frame.midi = 69.0f + 12.0f * std::log2 (frame.hz / 440.0f);
        frame.cents = (frame.midi - std::round (frame.midi)) * 100.0f;
    }
    return frame;
}

std::vector<PitchFrame> PitchTracker::analyseBuffer (const float* mono, int numSamples,
                                                     double sampleRate, double hopSeconds)
{
    std::vector<PitchFrame> frames;
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return frames;

    const int factor = decimationFactor (sampleRate);
    std::vector<float> low;
    decimate (mono, numSamples, factor, low);

    const double rate = sampleRate / factor;
    const int hop = juce::jmax (1, (int) (rate * hopSeconds));
    frames.reserve ((size_t) juce::jmax (0, ((int) low.size() - frameSize) / hop + 1));

    for (int start = 0; start + frameSize <= (int) low.size(); start += hop)
        frames.push_back (trackFrame (low.data() + start, frameSize, rate));

    return frames;
}

} // namespace keyglo
