#include "BeatKeyDetector.h"

namespace keyglo
{

namespace
{
    constexpr int fftOrder = 15;                 // 32768
    constexpr int fftSize  = 1 << fftOrder;
    constexpr int hopSize  = fftSize / 2;

    struct Peak { double freq; float weight; };

    // Krumhansl-Schmuckler key profiles.
    const float majorProfile[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
                                     2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    const float minorProfile[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
                                     2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float correlate (const std::array<float, 12>& chroma, const float* profile, int rotation)
    {
        double cMean = 0.0, pMean = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            cMean += chroma[(size_t) i];
            pMean += profile[i];
        }
        cMean /= 12.0; pMean /= 12.0;

        double num = 0.0, cDen = 0.0, pDen = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double c = chroma[(size_t) ((i + rotation) % 12)] - cMean;
            const double p = profile[i] - pMean;
            num += c * p; cDen += c * c; pDen += p * p;
        }
        const double den = std::sqrt (cDen * pDen);
        return den > 1.0e-9 ? (float) (num / den) : 0.0f;
    }
}

BeatKeyResult BeatKeyDetector::analyse (const float* mono, int numSamples, double sampleRate)
{
    BeatKeyResult result;
    if (mono == nullptr || sampleRate <= 0.0 || numSamples < fftSize)
        return result;

    numSamples = juce::jmin (numSamples, (int) (sampleRate * 30.0));

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) i / (float) (fftSize - 1));

    std::vector<float> fftData ((size_t) fftSize * 2);
    std::vector<Peak> allPeaks;
    allPeaks.reserve (4096);

    const double binHz = sampleRate / (double) fftSize;
    const int loBin = juce::jmax (2, (int) std::ceil (28.0 / binHz));
    const int hiBin = juce::jmin (fftSize / 2 - 2, (int) (5000.0 / binHz));

    double totalPeakEnergy = 0.0, totalFrameEnergy = 0.0;

    for (int start = 0; start + fftSize <= numSamples; start += hopSize)
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = mono[start + i] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // Adaptive floor: median magnitude in the band of interest.
        std::vector<float> mags (fftData.begin() + loBin, fftData.begin() + hiBin);
        std::nth_element (mags.begin(), mags.begin() + (long) mags.size() / 2, mags.end());
        const float median = mags[mags.size() / 2];
        const float threshold = juce::jmax (1.0e-5f, median * 4.0f);

        for (int b = loBin; b < hiBin; ++b)
        {
            const float m = fftData[(size_t) b];
            totalFrameEnergy += m;
            if (m < threshold
                 || m <= fftData[(size_t) b - 1] || m < fftData[(size_t) b + 1])
                continue;

            // Parabolic interpolation for the exact peak frequency.
            const float l = fftData[(size_t) b - 1], r = fftData[(size_t) b + 1];
            const float denom = l - 2.0f * m + r;
            const float delta = std::abs (denom) > 1.0e-9f
                                  ? juce::jlimit (-0.5f, 0.5f, 0.5f * (l - r) / denom)
                                  : 0.0f;
            const double freq = ((double) b + delta) * binHz;

            // Compressed magnitude so sustained harmonics dominate; extra
            // weight for the bass register (the tonic evidence).
            float weight = std::pow (m, 0.6f);
            if (freq < 250.0)
                weight *= 2.2f;

            allPeaks.push_back ({ freq, weight });

            // Tonal-energy share counts the peak's neighbourhood too: a bass
            // note that is amplitude/pitch-modulated by the groove (every 808)
            // spreads into sidebands that are not local maxima themselves.
            for (int nb = juce::jmax (loBin, b - 2); nb <= juce::jmin (hiBin - 1, b + 2); ++nb)
                totalPeakEnergy += fftData[(size_t) nb];
        }
    }

    if (allPeaks.empty())
        return result;

    // --- tuning offset: weighted circular mean of cents deviations ---------
    {
        double sx = 0.0, sy = 0.0;
        for (const auto& p : allPeaks)
        {
            const double midi = 69.0 + 12.0 * std::log2 (p.freq / 440.0);
            const double dev = (midi - std::round (midi)) * 100.0;   // -50..50
            const double angle = dev / 50.0 * juce::MathConstants<double>::pi;
            sx += p.weight * std::cos (angle);
            sy += p.weight * std::sin (angle);
        }
        if (sx != 0.0 || sy != 0.0)
            result.tuningCents = (float) (std::atan2 (sy, sx)
                                            / juce::MathConstants<double>::pi * 50.0);
    }

    // --- chroma with the tuning compensated --------------------------------
    std::array<double, 12> chroma {};
    for (const auto& p : allPeaks)
    {
        const double midi = 69.0 + 12.0 * std::log2 (p.freq / 440.0)
                          - result.tuningCents / 100.0;
        const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
        chroma[(size_t) pc] += p.weight;
    }

    double maxChroma = 0.0;
    for (auto v : chroma) maxChroma = juce::jmax (maxChroma, v);
    if (maxChroma <= 0.0)
        return result;
    for (int i = 0; i < 12; ++i)
        result.chroma[(size_t) i] = (float) (chroma[(size_t) i] / maxChroma);

    // --- reliability gates --------------------------------------------------
    // Percussion/noise: peaks carry little of the spectrum, or the chroma is
    // nearly flat (every pitch class equally present).
    const double peakShare = totalFrameEnergy > 0.0
                               ? juce::jmin (1.0, totalPeakEnergy / totalFrameEnergy) : 0.0;

    double mean = 0.0;
    for (auto v : result.chroma) mean += v;
    mean /= 12.0;
    double variance = 0.0;
    for (auto v : result.chroma) variance += (v - mean) * (v - mean);
    variance /= 12.0;
    const double spread = std::sqrt (variance) / juce::jmax (0.05, mean);

    // --- template scoring ---------------------------------------------------
    struct Scored { int root; bool minor; float r; };
    std::vector<Scored> scored;
    scored.reserve (24);
    for (int root = 0; root < 12; ++root)
    {
        scored.push_back ({ root, false, correlate (result.chroma, majorProfile, root) });
        scored.push_back ({ root, true,  correlate (result.chroma, minorProfile, root) });
    }
    std::sort (scored.begin(), scored.end(),
               [] (const Scored& a, const Scored& b) { return a.r > b.r; });

    const auto& best = scored.front();

    // Margin against the best candidate with a DIFFERENT root (the same
    // root's other mode is a much weaker distinction than a rival root).
    float rivalR = -1.0f;
    for (const auto& s : scored)
        if (s.root != best.root) { rivalR = s.r; break; }

    result.rootPc = best.root;
    result.minor = best.minor;
    result.confidence = juce::jlimit (0.0f, 1.0f,
                                      0.55f * best.r + 1.8f * (best.r - rivalR));

    // How much chroma energy sits inside the winning key's scale. This is
    // the tonal-vs-noise discriminator that survives rich full-scale
    // harmony: a produced groove covering all seven scale degrees has a
    // fairly FLAT chroma (spread ~0.3), but still keeps >85 % of its energy
    // in scale, where flat noise can only reach 7/12.
    double inScale = 0.0, total = 0.0;
    {
        const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
        bool member[12] = {};
        for (int i = 0; i < 7; ++i)
            member[(best.root + (best.minor ? minorSteps[i] : majorSteps[i])) % 12] = true;
        for (int i = 0; i < 12; ++i)
        {
            total += result.chroma[(size_t) i];
            if (member[i])
                inScale += result.chroma[(size_t) i];
        }
    }
    const double inScaleShare = total > 0.0 ? inScale / total : 0.0;

    result.gatePeakShare = (float) peakShare;
    result.gateSpread = (float) inScaleShare;
    result.gateBestR = best.r;

    // Thresholds set from MEASURED material, synthetic and real:
    //
    //                       peakShare  inScale  bestR  conf
    //   real production A      0.63      0.68    0.43  0.26   must pass
    //   real production B      0.58      0.73    0.68  0.38   must pass
    //   drum loop              0.28      0.62    0.39  0.29   must fail
    //   white noise            0.00      0.00    0.00  0.00   must fail
    //   synthetic chord loops  0.87      0.90+   0.73+ 0.5+    must pass
    //
    // peakShare - the share of spectral energy sitting in resolved peaks -
    // is the discriminator that actually separates pitched music from
    // percussion (2x margin). bestR and confidence do NOT: a drum loop can
    // out-correlate a real production on both, because a sparse chroma
    // matches some template well. The original gates were calibrated on
    // clean synthetic chords only, and would have shown NO RELIABLE KEY on a
    // real record whose key it had detected correctly.
    result.reliable = peakShare > 0.45
                       && inScaleShare > 0.62
                       && best.r > 0.35f
                       && spread > 0.25
                       && result.confidence > 0.20f;

    for (size_t i = 1; i < scored.size() && result.alternatives.size() < 3; ++i)
    {
        const auto& s = scored[i];
        if (s.root == best.root && s.minor != best.minor)
            continue;   // relative modes of the winner are not useful alternatives
        result.alternatives.push_back (
            { s.root, s.minor,
              juce::jlimit (0.0f, 1.0f, 0.55f * s.r + 1.8f * (s.r - best.r) + 0.35f) });
    }

    return result;
}

} // namespace keyglo
