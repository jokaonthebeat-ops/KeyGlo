/*
    VocalEngine - the message-thread-free vocal side: continuous pitch
    tracking of the capture ring, the guided range test state machine, and
    hook-fit scoring.

    Runs inside the AnalysisCoordinator's worker. Publishes the live note,
    cents and the 12-second pitch trail into LiveVisuals, and profile/fit
    results into the snapshot.

    The range test is a guided flow (comfortable low -> centre -> high ->
    optional falsetto). Each phase collects only STABLE voiced frames -
    a phase advances on the user's click, so the artist controls pacing.
*/

#pragma once
#include <JuceHeader.h>
#include "PitchTracker.h"
#include "VocalRangeProfiler.h"
#include "HookFitScorer.h"
#include <array>
#include <atomic>
#include <deque>

namespace keyglo
{

enum class RangeTestPhase { idle = 0, low, centre, high, falsetto, done };

class VocalEngine
{
public:
    static constexpr double hopSeconds = 1.0 / 45.0;   // trail rate

    // --- worker side ------------------------------------------------------
    // Called with the newest mono audio at `rate`; returns the newest frame.
    PitchFrame processChunk (const float* mono, int numSamples, double rate)
    {
        PitchFrame latest;
        if (mono == nullptr || numSamples <= 0)
            return latest;

        const auto frames = PitchTracker::analyseBuffer (mono, numSamples, rate, hopSeconds);
        for (const auto& f : frames)
        {
            pushTrail (f);
            if (f.voiced)
                latest = f;

            // Range-test collection: stable voiced frames only.
            const auto phase = (RangeTestPhase) currentPhase.load();
            if (phase != RangeTestPhase::idle && phase != RangeTestPhase::done
                 && f.voiced && f.confidence > 0.72f)
            {
                auto& bucket = phaseBucket (phase);
                if (bucket.size() < 4000)
                    bucket.push_back (f.midi);
            }

            // Hook capture: every voiced frame while armed.
            if (hookArmed.load())
                hookFrames.push_back (f);
        }
        return latest;
    }

    // 12 seconds of history, oldest first; <= 0 marks unvoiced gaps.
    void fillTrail (std::array<float, 540>& dest) const
    {
        const juce::ScopedLock sl (trailLock);
        dest.fill (-1.0f);
        const int n = juce::jmin ((int) trail.size(), 540);
        for (int i = 0; i < n; ++i)
            dest[(size_t) (540 - n + i)] = trail[trail.size() - (size_t) n + (size_t) i];
    }

    // --- range test (message thread calls, worker reads atomically) -------
    void startRangeTest()
    {
        clearBuckets();
        currentPhase.store ((int) RangeTestPhase::low);
    }

    void advanceRangeTest()
    {
        const auto phase = (RangeTestPhase) currentPhase.load();
        switch (phase)
        {
            case RangeTestPhase::low:      currentPhase.store ((int) RangeTestPhase::centre); break;
            case RangeTestPhase::centre:   currentPhase.store ((int) RangeTestPhase::high); break;
            case RangeTestPhase::high:     currentPhase.store ((int) RangeTestPhase::falsetto); break;
            case RangeTestPhase::falsetto: finishRangeTest(); break;
            default: break;
        }
    }

    void cancelRangeTest()
    {
        currentPhase.store ((int) RangeTestPhase::idle);
        clearBuckets();
    }

    // Ends the test, building the profile from whatever phases have data.
    bool finishRangeTest()
    {
        const bool enough = lowPitches.size() > 20 && midPitches.size() > 20
                              && highPitches.size() > 20;
        if (enough)
        {
            profile = VocalRangeProfiler::build (lowPitches, midPitches,
                                                 highPitches, falsettoPitches);
            profileValid = true;
        }
        currentPhase.store ((int) RangeTestPhase::done);
        return enough;
    }

    RangeTestPhase phase() const  { return (RangeTestPhase) currentPhase.load(); }
    int phaseFrameCount() const
    {
        switch (phase())
        {
            case RangeTestPhase::low:      return (int) lowPitches.size();
            case RangeTestPhase::centre:   return (int) midPitches.size();
            case RangeTestPhase::high:     return (int) highPitches.size();
            case RangeTestPhase::falsetto: return (int) falsettoPitches.size();
            default: return 0;
        }
    }

    // --- hook -------------------------------------------------------------
    void armHook()      { hookFrames.clear(); hookArmed.store (true); }
    void disarmHook()   { hookArmed.store (false); }
    bool isHookArmed() const { return hookArmed.load(); }

    HookFitResult scoreHook (const std::array<bool, 12>& beatScale, bool haveScale) const
    {
        const auto stats = HookStats::fromFrames (hookFrames, hopSeconds);
        return HookFitScorer::score (stats, profile, beatScale, haveScale);
    }

    // Continuous fit: score the trail's recent voiced history as the "hook"
    // so the pods respond while someone sings, without an explicit capture.
    HookFitResult scoreRecent (const std::array<bool, 12>& beatScale, bool haveScale) const
    {
        std::vector<PitchFrame> frames;
        {
            const juce::ScopedLock sl (trailLock);
            frames.reserve (trail.size());
            for (float midi : trail)
            {
                PitchFrame f;
                f.voiced = midi > 0.0f;
                f.midi = midi;
                frames.push_back (f);
            }
        }
        const auto stats = HookStats::fromFrames (frames, hopSeconds);
        return HookFitScorer::score (stats, profile, beatScale, haveScale);
    }

    const ArtistProfile& getProfile() const  { return profile; }
    bool hasProfile() const                  { return profileValid; }
    void setProfile (const ArtistProfile& p) { profile = p; profileValid = true; }

private:
    void pushTrail (const PitchFrame& f)
    {
        const juce::ScopedLock sl (trailLock);
        trail.push_back (f.voiced ? f.midi : -1.0f);
        while (trail.size() > 540)
            trail.pop_front();
    }

    std::vector<float>& phaseBucket (RangeTestPhase p)
    {
        switch (p)
        {
            case RangeTestPhase::low:      return lowPitches;
            case RangeTestPhase::centre:   return midPitches;
            case RangeTestPhase::high:     return highPitches;
            default:                       return falsettoPitches;
        }
    }

    void clearBuckets()
    {
        lowPitches.clear(); midPitches.clear();
        highPitches.clear(); falsettoPitches.clear();
    }

    mutable juce::CriticalSection trailLock;
    std::deque<float> trail;

    std::atomic<int> currentPhase { (int) RangeTestPhase::idle };
    std::vector<float> lowPitches, midPitches, highPitches, falsettoPitches;

    std::atomic<bool> hookArmed { false };
    std::vector<PitchFrame> hookFrames;

    ArtistProfile profile;
    bool profileValid = false;
};

} // namespace keyglo
