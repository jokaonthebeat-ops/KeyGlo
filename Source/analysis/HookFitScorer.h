/*
    HookFitScorer - scores a performed hook against the artist profile and
    the beat's scale, across all candidate transpositions.

    The recommendation transposes the BEAT; the artist then sings the hook
    shifted by the same amount, so the range score moves with the candidate
    while scale compatibility is transposition-invariant (hook and scale
    shift together). Normalised 0..1; ties prefer the smaller shift
    (DSP_AND_ANALYSIS_ARCHITECTURE.md hook-fit section).
*/

#pragma once
#include <JuceHeader.h>
#include "VocalRangeProfiler.h"
#include "PitchTracker.h"
#include <array>

namespace keyglo
{

struct HookStats
{
    std::array<float, 128> noteSeconds {};   // voiced time per MIDI note
    float totalVoicedSeconds = 0.0f;
    float medianMidi = 0.0f;

    static HookStats fromFrames (const std::vector<PitchFrame>& frames, double hopSeconds)
    {
        HookStats stats;
        std::vector<float> voiced;
        for (const auto& f : frames)
        {
            if (! f.voiced)
                continue;
            const int note = juce::jlimit (0, 127, juce::roundToInt (f.midi));
            stats.noteSeconds[(size_t) note] += (float) hopSeconds;
            stats.totalVoicedSeconds += (float) hopSeconds;
            voiced.push_back (f.midi);
        }
        if (! voiced.empty())
            stats.medianMidi = VocalRangeProfiler::percentile (voiced, 0.5f);
        return stats;
    }
};

struct HookFitResult
{
    bool valid = false;
    float artistFit = 0.0f;       // overall fit at the original pitch
    float rangeFit = 0.0f;        // range-only score at the original pitch
    float hookMatch = 0.0f;       // scale compatibility with the beat
    int recommendedTranspose = 0; // -4..+4
    float estimatedFit = 0.0f;    // overall fit at the recommendation
    std::array<float, 9> fitByTranspose {};   // index 0 = -4 ... 8 = +4
};

class HookFitScorer
{
public:
    static HookFitResult score (const HookStats& hook, const ArtistProfile& profile,
                                const std::array<bool, 12>& beatScale, bool haveScale)
    {
        HookFitResult result;
        if (hook.totalVoicedSeconds < 1.5f)
            return result;

        // Scale compatibility is transposition-invariant (see header note).
        float inScaleSeconds = 0.0f;
        for (int note = 0; note < 128; ++note)
            if (! haveScale || beatScale[(size_t) (note % 12)])
                inScaleSeconds += hook.noteSeconds[(size_t) note];
        result.hookMatch = haveScale
                             ? juce::jlimit (0.0f, 1.0f, inScaleSeconds / hook.totalVoicedSeconds)
                             : 0.0f;

        for (int t = -4; t <= 4; ++t)
        {
            const float range = rangeScore (hook, profile, t);
            const float overall = haveScale
                                    ? juce::jlimit (0.0f, 1.0f, 0.70f * range + 0.30f * result.hookMatch)
                                    : range;
            result.fitByTranspose[(size_t) (t + 4)] = overall;
        }

        result.rangeFit = rangeScore (hook, profile, 0);
        result.artistFit = result.fitByTranspose[4];

        // Best transposition; ties within 0.02 prefer the smaller shift.
        int best = 0;
        for (int t = -4; t <= 4; ++t)
        {
            const float candidate = result.fitByTranspose[(size_t) (t + 4)];
            const float incumbent = result.fitByTranspose[(size_t) (best + 4)];
            if (candidate > incumbent + 0.02f
                 || (candidate > incumbent - 0.02f && std::abs (t) < std::abs (best)))
                best = t;
        }
        result.recommendedTranspose = best;
        result.estimatedFit = result.fitByTranspose[(size_t) (best + 4)];
        result.valid = true;
        return result;
    }

private:
    static float rangeScore (const HookStats& hook, const ArtistProfile& profile,
                             int transpose)
    {
        float strong = 0.0f, comfortable = 0.0f, extended = 0.0f, sustainedOut = 0.0f;

        for (int note = 0; note < 128; ++note)
        {
            const float seconds = hook.noteSeconds[(size_t) note];
            if (seconds <= 0.0f)
                continue;
            const int n = note + transpose;

            const bool inExtended = n >= profile.extendedLowMidi
                                     && n <= (profile.hasFalsetto ? profile.falsettoHighMidi
                                                                  : profile.extendedHighMidi);
            if (inExtended)
            {
                extended += seconds;
                if (n >= profile.comfortableLowMidi && n <= profile.comfortableHighMidi)
                    comfortable += seconds;
                if (n >= profile.strongLowMidi && n <= profile.strongHighMidi)
                    strong += seconds;
            }
            else if (seconds > 0.35f)
            {
                // Sustained notes beyond the profile hurt more than passing
                // ones ("sustained notes near/outside limits").
                sustainedOut += seconds;
            }
        }

        const float total = juce::jmax (0.001f, hook.totalVoicedSeconds);
        const float centreDistance = std::abs (hook.medianMidi + (float) transpose
                                                 - profile.strongCentre());

        float score = 0.50f * (strong / total)
                    + 0.30f * (comfortable / total)
                    + 0.20f * (extended / total)
                    - 0.50f * (sustainedOut / total)
                    - 0.015f * centreDistance;
        return juce::jlimit (0.0f, 1.0f, score);
    }
};

} // namespace keyglo
