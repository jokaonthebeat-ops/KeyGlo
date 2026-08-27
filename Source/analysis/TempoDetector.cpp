#include "TempoDetector.h"

namespace keyglo
{

TempoResult TempoDetector::analyse (const float* mono, int numSamples, double sampleRate)
{
    TempoResult result;

    constexpr int fftOrder = 10;              // 1024
    constexpr int fftSize = 1 << fftOrder;
    constexpr int hop = fftSize / 2;

    if (mono == nullptr || sampleRate <= 0.0 || numSamples < (int) (sampleRate * 4.0))
        return result;                        // needs ~4 s to say anything

    numSamples = juce::jmin (numSamples, (int) (sampleRate * 30.0));
    const double fluxRate = sampleRate / (double) hop;

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) i / (float) (fftSize - 1));

    // --- spectral-flux onset envelope --------------------------------------
    std::vector<float> fftData ((size_t) fftSize * 2);
    std::vector<float> previous ((size_t) fftSize / 2, 0.0f);
    std::vector<float> flux;
    flux.reserve ((size_t) (numSamples / hop) + 1);

    for (int start = 0; start + fftSize <= numSamples; start += hop)
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = mono[start + i] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        float sum = 0.0f;
        for (int b = 1; b < fftSize / 2; ++b)
        {
            const float m = std::log10 (1.0f + 10.0f * fftData[(size_t) b]);
            const float d = m - previous[(size_t) b];
            if (d > 0.0f)
                sum += d;
            previous[(size_t) b] = m;
        }
        flux.push_back (sum);
    }

    if (flux.size() < 64)
        return result;

    // Detrend with a ~0.5 s moving average, half-wave rectify.
    const int avgLen = juce::jmax (1, (int) (fluxRate * 0.5));
    std::vector<float> onset (flux.size(), 0.0f);
    double acc = 0.0;
    for (int i = 0; i < (int) flux.size(); ++i)
    {
        acc += flux[(size_t) i];
        if (i >= avgLen)
            acc -= flux[(size_t) (i - avgLen)];
        const float avg = (float) (acc / juce::jmin (i + 1, avgLen));
        onset[(size_t) i] = juce::jmax (0.0f, flux[(size_t) i] - avg);
    }

    // --- autocorrelation over the BPM range --------------------------------
    const int minLag = juce::jmax (1, (int) std::floor (fluxRate * 60.0 / 220.0));
    const int maxLag = juce::jmin ((int) onset.size() / 2,
                                   (int) std::ceil (fluxRate * 60.0 / 55.0));
    if (maxLag <= minLag)
        return result;

    double energy = 0.0;
    for (auto v : onset) energy += (double) v * v;
    if (energy < 1.0e-9)
        return result;

    std::vector<float> ac ((size_t) maxLag + 1, 0.0f);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = lag; i < (int) onset.size(); ++i)
            s += (double) onset[(size_t) i] * onset[(size_t) (i - lag)];
        ac[(size_t) lag] = (float) (s / energy);
    }

    // Mild preference for the hip-hop/R&B pocket so the metrical level lands
    // where a producer expects it (raw autocorrelation often prefers half
    // time). Gaussian window centred near 105 BPM, gentle.
    auto weighted = [&] (int lag)
    {
        const double bpm = 60.0 * fluxRate / lag;
        const double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 105.0) / 1.1, 2.0));
        return ac[(size_t) lag] * (float) (0.35 + 0.65 * w);
    };

    int bestLag = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (weighted (lag) > weighted (bestLag))
            bestLag = lag;

    // Autocorrelation inherently favours slower levels (a longer lag inherits
    // every shorter periodicity), so walk down to the fastest metrical level
    // whose evidence is nearly as strong - a groove with an onset on every
    // beat should report the beat, not the half-note.
    while (bestLag / 2 >= minLag
            && ac[(size_t) (bestLag / 2)] >= 0.82f * ac[(size_t) bestLag])
        bestLag /= 2;

    // Parabolic refinement of the lag.
    double lagRefined = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const float l = ac[(size_t) bestLag - 1], m = ac[(size_t) bestLag],
                    r = ac[(size_t) bestLag + 1];
        const float denom = l - 2.0f * m + r;
        if (std::abs (denom) > 1.0e-9f)
            lagRefined += juce::jlimit (-0.5f, 0.5f, 0.5f * (l - r) / denom);
    }

    const float bpm = (float) (60.0 * fluxRate / lagRefined);
    result.bpm = bpm;
    result.candidates.push_back (bpm);

    // Half/double-time evidence.
    auto acAt = [&] (double lag) -> float
    {
        const int i = (int) std::lround (lag);
        return i >= minLag && i <= maxLag ? ac[(size_t) i] : 0.0f;
    };
    const float bestA = ac[(size_t) bestLag];
    const float halfA = acAt (lagRefined * 2.0);    // half tempo
    const float dblA  = acAt (lagRefined * 0.5);    // double tempo

    if (halfA > bestA * 0.75f && bpm * 0.5f >= 55.0f)
        result.candidates.push_back (bpm * 0.5f);
    if (dblA > bestA * 0.75f && bpm * 2.0f <= 220.0f)
        result.candidates.push_back (bpm * 2.0f);

    // Confidence: how much the beat lag stands out of the mean correlation.
    double meanAc = 0.0; int count = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) { meanAc += ac[(size_t) lag]; ++count; }
    meanAc /= juce::jmax (1, count);
    result.confidence = juce::jlimit (0.0f, 1.0f,
                                      (float) ((bestA - meanAc) / juce::jmax (0.05, meanAc * 2.0)));
    result.reliable = bestA > 0.05f && result.confidence > 0.15f;

    return result;
}

} // namespace keyglo
