/*
    VocalRangeProfiler - builds an artist profile from the guided range
    test's collected pitches. Stable percentile statistics, never one
    extreme note (DSP_AND_ANALYSIS_ARCHITECTURE.md): extended ends are the
    5th/95th percentiles of the low/high phases, comfortable the 30th/70th,
    and the strong zone the central mass of the mid phase.

    No medical wording anywhere - zones say "near top of comfort zone",
    never strain claims.
*/

#pragma once
#include <JuceHeader.h>
#include <vector>
#include <algorithm>

namespace keyglo
{

struct ArtistProfile
{
    int comfortableLowMidi = 48, comfortableHighMidi = 67;
    int strongLowMidi = 55, strongHighMidi = 64;
    int extendedLowMidi = 43, extendedHighMidi = 72;
    int falsettoHighMidi = 72;      // == extendedHigh when no falsetto pass
    bool hasFalsetto = false;

    float strongCentre() const
    {
        return 0.5f * (float) (strongLowMidi + strongHighMidi);
    }
};

class VocalRangeProfiler
{
public:
    static float percentile (std::vector<float> values, float p01)
    {
        if (values.empty())
            return 0.0f;
        std::sort (values.begin(), values.end());
        const float idx = p01 * (float) (values.size() - 1);
        const int lo = (int) idx;
        const int hi = juce::jmin ((int) values.size() - 1, lo + 1);
        const float frac = idx - (float) lo;
        return values[(size_t) lo] * (1.0f - frac) + values[(size_t) hi] * frac;
    }

    // Each vector holds voiced MIDI pitches collected during that phase.
    // Falsetto may be empty (skipped).
    static ArtistProfile build (const std::vector<float>& lowPhase,
                                const std::vector<float>& midPhase,
                                const std::vector<float>& highPhase,
                                const std::vector<float>& falsettoPhase)
    {
        ArtistProfile p;
        if (lowPhase.empty() || midPhase.empty() || highPhase.empty())
            return p;   // defaults; caller checks enough data was collected

        p.extendedLowMidi    = juce::roundToInt (percentile (lowPhase, 0.05f));
        p.comfortableLowMidi = juce::roundToInt (percentile (lowPhase, 0.30f));
        p.extendedHighMidi   = juce::roundToInt (percentile (highPhase, 0.95f));
        p.comfortableHighMidi= juce::roundToInt (percentile (highPhase, 0.70f));

        p.strongLowMidi  = juce::roundToInt (percentile (midPhase, 0.20f));
        p.strongHighMidi = juce::roundToInt (percentile (midPhase, 0.80f));

        // Enforce sane nesting whatever the singer did.
        p.comfortableLowMidi = juce::jmax (p.comfortableLowMidi, p.extendedLowMidi);
        p.comfortableHighMidi= juce::jmin (p.comfortableHighMidi, p.extendedHighMidi);
        p.strongLowMidi  = juce::jlimit (p.comfortableLowMidi, p.comfortableHighMidi,
                                         p.strongLowMidi);
        p.strongHighMidi = juce::jlimit (p.strongLowMidi, p.comfortableHighMidi,
                                         p.strongHighMidi);

        if (! falsettoPhase.empty())
        {
            p.hasFalsetto = true;
            p.falsettoHighMidi = juce::jmax (p.extendedHighMidi,
                                             juce::roundToInt (percentile (falsettoPhase, 0.90f)));
        }
        else
        {
            p.falsettoHighMidi = p.extendedHighMidi;
        }
        return p;
    }
};

} // namespace keyglo
