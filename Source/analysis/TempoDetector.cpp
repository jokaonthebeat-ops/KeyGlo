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
    auto perceptual = [&] (double lag)
    {
        const double bpm = 60.0 * fluxRate / lag;
        return 0.35 + 0.65 * std::exp (-0.5 * std::pow (std::log2 (bpm / 105.0) / 1.1, 2.0));
    };

    auto acAtLag = [&] (double lag) -> double
    {
        const int i = (int) std::lround (lag);
        return (i >= minLag && i <= maxLag) ? ac[(size_t) i] : 0.0;
    };

    // How well a candidate beat period explains the envelope: a true beat has
    // peaks not only at its own lag but at its multiples, so score a short
    // comb. Judging a candidate by its own lag alone is what makes a detector
    // prefer whatever periodicity happens to be strongest, which on real
    // productions is often a triplet subdivision rather than the beat.
    auto combScore = [&] (double lag)
    {
        // Weighted SUM, not an average: a candidate is rewarded for having
        // support at its multiples, and must not be penalised merely for
        // having more of them in range. Averaging did exactly that - it made
        // slow candidates (few multiples below maxLag) look better than the
        // beat, which is the opposite of the intent.
        static const double w[4] = { 1.0, 0.6, 0.35, 0.2 };
        double sum = 0.0;
        for (int m = 1; m <= 4; ++m)
        {
            const double l = lag * m;
            if (l > maxLag)
                break;
            sum += w[m - 1] * acAtLag (l);
        }
        return sum * perceptual (lag);
    };

    int rawBest = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] * perceptual (lag) > ac[(size_t) rawBest] * perceptual (rawBest))
            rawBest = lag;

    // From the dominant pulse to the beat, in two explicit steps. An earlier
    // version searched a basket of metrical ratios by comb score; it was not
    // decisive (132 and 176 landed within 3 % of each other) and the layered
    // heuristics interfered with one another. Two named corrections, each
    // with a measured justification, are easier to reason about and to test.
    double bestLagD = rawBest;

    // 1. Dotted-pulse correction. In modern trap the 3-3-2 pattern makes the
    //    DOTTED QUARTER the strongest periodicity in the envelope, not the
    //    beat. Measured on two real productions: 88 BPM outscored the true
    //    132 (0.189 vs 0.156), and 84 outscored the true 126 (0.158 vs
    //    0.120) - in both cases the real tempo sat at exactly 3/2 of the
    //    dominant peak. When the 3/2 relative still carries most of the
    //    peak's support, the peak was a subdivision. A straight groove has
    //    little energy there and is left alone.
    {
        const double faster = bestLagD * (2.0 / 3.0);
        const double fasterBpm = 60.0 * fluxRate / faster;
        if (faster >= minLag && fasterBpm <= 200.0
             && acAtLag (faster) >= 0.70 * acAtLag (bestLagD))
            bestLagD = faster;
    }

    // 2. Octave walk-down: autocorrelation inherently favours slower levels
    //    (a longer lag inherits every shorter periodicity), so drop to the
    //    fastest level whose evidence is nearly as strong - a groove with an
    //    onset on every beat should report the beat, not the half-note.
    while (bestLagD / 2.0 >= minLag
            && acAtLag (bestLagD / 2.0) >= 0.82 * acAtLag (bestLagD))
        bestLagD /= 2.0;

    int bestLag = juce::jlimit (minLag, maxLag, (int) std::lround (bestLagD));

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

    // The dominant pulse itself, when a correction moved off it: hearing the
    // groove at the dotted level is a legitimate reading, not an error.
    const double rawBpm = 60.0 * fluxRate / rawBest;
    if (std::abs (rawBpm - bpm) > 1.0 && rawBpm >= 55.0 && rawBpm <= 220.0)
        result.candidates.push_back ((float) rawBpm);

    // Confidence is measured at the DOMINANT pulse, not at the corrected lag.
    // Two different questions live here: "is there a clear periodic pulse?"
    // (what makes a tempo readable at all) and "which metrical level do we
    // name?" (genuinely harder, and what the corrections above decide).
    // Scoring the first at the corrected lag conflated them and reported
    // real, cleanly-pulsed beats as unreliable.
    double meanAc = 0.0; int count = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) { meanAc += ac[(size_t) lag]; ++count; }
    meanAc /= juce::jmax (1, count);
    const float pulseA = ac[(size_t) rawBest];
    result.confidence = juce::jlimit (0.0f, 1.0f,
                                      (float) ((pulseA - meanAc) / juce::jmax (0.05, meanAc * 2.0)));
    result.reliable = pulseA > 0.05f && result.confidence > 0.15f;
    result.acCurve = ac;
    result.fluxRate = fluxRate;

    return result;
}

} // namespace keyglo
