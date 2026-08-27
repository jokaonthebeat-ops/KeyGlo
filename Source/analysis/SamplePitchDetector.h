/*
    SamplePitchDetector - the stable pitch of 808s, one-shots and loops.

    Pipeline (DSP_AND_ANALYSIS_ARCHITECTURE.md, 808 section): find the
    transient, skip the attack, analyse the SUSTAIN with the pitch tracker
    (decimated to 8 kHz so the floor reaches ~25 Hz), take robust median
    statistics, and compare early vs late sustain - a substantial glide is
    reported as a pitch envelope, with start and sustain notes, instead of
    pretending the sample has one note.

    The recommendation tunes the sample to the nearest tone of the beat's
    scale when a key is known, otherwise to the nearest exact semitone.
*/

#pragma once
#include <JuceHeader.h>
#include "PitchTracker.h"
#include "VocalRangeProfiler.h"
#include <array>

namespace keyglo
{

struct SampleTuneResult
{
    bool valid = false;
    bool pitchEnvelope = false;

    float detectedMidi = 0.0f;        // sustain median, fractional
    float deviationCents = 0.0f;      // from the nearest semitone (the needle)
    int recommendedSemitones = 0;     // integer part of the shift to target
    float fineTuneCents = 0.0f;       // fractional remainder of that shift
    float totalShiftSemitones = 0.0f; // exact shift Apply Tune renders
    float confidence = 0.0f;

    int startNoteMidi = 0;            // only meaningful with pitchEnvelope

    static juce::String midiNoteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

    juce::String noteName() const      { return midiNoteName (juce::roundToInt (detectedMidi)); }
    juce::String startNoteName() const { return midiNoteName (startNoteMidi); }
};

class SamplePitchDetector
{
public:
    static SampleTuneResult analyse (const float* mono, int numSamples, double sampleRate,
                                     const std::array<bool, 12>& beatScale, bool haveScale)
    {
        SampleTuneResult result;
        if (mono == nullptr || sampleRate <= 0.0
             || numSamples < (int) (sampleRate * 0.15))
            return result;

        // --- transient: first block where energy jumps out of the floor ----
        const int hop = (int) (sampleRate * 0.005);
        float peakRms = 0.0f;
        std::vector<float> rms;
        for (int start = 0; start + hop <= numSamples; start += hop)
        {
            double e = 0.0;
            for (int i = 0; i < hop; ++i)
                e += (double) mono[start + i] * mono[start + i];
            const float value = (float) std::sqrt (e / hop);
            rms.push_back (value);
            peakRms = juce::jmax (peakRms, value);
        }
        if (peakRms < 1.0e-4f)
            return result;

        int onsetBlock = 0;
        for (int i = 0; i < (int) rms.size(); ++i)
            if (rms[(size_t) i] > peakRms * 0.25f)
            {
                onsetBlock = i;
                break;
            }

        // Sustain starts ~40 ms past the onset (past the click/pitch drop of
        // an 808 attack) and runs while the level holds above the floor.
        const int sustainStart = juce::jmin (numSamples - 1,
                                             onsetBlock * hop + (int) (sampleRate * 0.04));
        int sustainEnd = sustainStart;
        for (int i = onsetBlock; i < (int) rms.size(); ++i)
        {
            if (rms[(size_t) i] < peakRms * 0.05f && i * hop > sustainStart)
                break;
            sustainEnd = juce::jmin (numSamples, (i + 1) * hop);
        }
        if (sustainEnd - sustainStart < (int) (sampleRate * 0.10))
            return result;

        // --- pitch over the sustain, decimated to ~8 kHz for the 808 floor -
        const int factor = juce::jmax (1, (int) std::round (sampleRate / 8000.0));
        std::vector<float> low;
        PitchTracker::decimate (mono + sustainStart, sustainEnd - sustainStart, factor, low);
        const double rate = sampleRate / factor;

        const int frameHop = juce::jmax (1, (int) (rate * 0.01));
        std::vector<float> pitches;
        std::vector<float> earlyPitches, latePitches;
        const int frameCount = juce::jmax (0, ((int) low.size() - PitchTracker::frameSize) / frameHop + 1);

        for (int f = 0; f < frameCount; ++f)
        {
            const auto frame = PitchTracker::trackFrame (low.data() + f * frameHop,
                                                         PitchTracker::frameSize, rate,
                                                         25.0f, 2000.0f);
            if (! frame.voiced)
                continue;
            pitches.push_back (frame.midi);
            if (f < frameCount / 3)
                earlyPitches.push_back (frame.midi);
            else if (f >= (2 * frameCount) / 3)
                latePitches.push_back (frame.midi);
        }

        if ((int) pitches.size() < juce::jmax (6, frameCount / 4))
            return result;   // no stable tone in the sustain

        const float median = VocalRangeProfiler::percentile (pitches, 0.5f);
        const float spread = VocalRangeProfiler::percentile (pitches, 0.85f)
                           - VocalRangeProfiler::percentile (pitches, 0.15f);

        result.detectedMidi = median;
        result.confidence = juce::jlimit (0.0f, 1.0f, 1.0f - spread / 1.5f);

        // --- pitch envelope: early vs late sustain drift -------------------
        if (! earlyPitches.empty() && ! latePitches.empty())
        {
            const float drift = VocalRangeProfiler::percentile (earlyPitches, 0.5f)
                              - VocalRangeProfiler::percentile (latePitches, 0.5f);
            if (std::abs (drift) > 0.6f)   // > 60 cents of glide
            {
                result.pitchEnvelope = true;
                result.startNoteMidi = juce::roundToInt (
                    VocalRangeProfiler::percentile (earlyPitches, 0.5f));
            }
        }
        if (spread > 1.2f)
            result.pitchEnvelope = true;

        // --- recommendation ------------------------------------------------
        result.deviationCents = (median - std::round (median)) * 100.0f;

        float targetMidi = std::round (median);
        if (haveScale)
        {
            // Near-ties (a G between F# and G#) resolve DOWNWARD - tuning an
            // 808 down keeps the sub out of harm's way and matches how
            // producers actually resolve it.
            float bestDistance = 1.0e9f;
            for (int candidate = juce::roundToInt (median) - 6;
                 candidate <= juce::roundToInt (median) + 6; ++candidate)
            {
                if (! beatScale[(size_t) (((candidate % 12) + 12) % 12)])
                    continue;
                const float distance = std::abs ((float) candidate - median)
                                     + ((float) candidate > median ? 0.10f : 0.0f);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    targetMidi = (float) candidate;
                }
            }
        }

        result.totalShiftSemitones = targetMidi - median;
        result.recommendedSemitones = juce::roundToInt (result.totalShiftSemitones);
        result.fineTuneCents = (result.totalShiftSemitones
                                  - (float) result.recommendedSemitones) * 100.0f;
        result.valid = true;
        return result;
    }

    // Waveform preview: min/max envelope pairs for the panel's well.
    template <size_t N>
    static void envelope (const float* mono, int numSamples,
                          std::array<float, N * 2>& dest)
    {
        dest.fill (0.0f);
        if (mono == nullptr || numSamples <= 0)
            return;
        for (size_t bin = 0; bin < N; ++bin)
        {
            const int start = (int) ((juce::int64) numSamples * (juce::int64) bin / (juce::int64) N);
            const int end = (int) ((juce::int64) numSamples * (juce::int64) (bin + 1) / (juce::int64) N);
            float lo = 0.0f, hi = 0.0f;
            for (int i = start; i < end; ++i)
            {
                lo = juce::jmin (lo, mono[i]);
                hi = juce::jmax (hi, mono[i]);
            }
            dest[bin * 2] = lo;
            dest[bin * 2 + 1] = hi;
        }
    }
};

} // namespace keyglo
