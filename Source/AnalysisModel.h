/*
    AnalysisModel.h - the immutable display snapshot the UI reads, and the
    demo feed that animates the milestone-1 interface.

    The snapshot's field set and starting values are the pack's own placeholder
    dataset (08_LAYOUT/analysis_data_contract.json) - the same numbers the
    approved mockup displays, which makes the overlay QA a like-for-like
    comparison. Milestones 2-4 replace DemoFeed with the real engine
    publishing through the same AnalysisDisplayModel; nothing in the UI
    changes when that happens.
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

namespace keyglo
{

struct AnalysisSnapshot
{
    // --- beat -------------------------------------------------------------
    juce::String key = "F#";
    juce::String scale = "Minor";
    float bpm = 148.0f;
    float tuningCents = -4.0f;
    float keyConfidence = 0.92f;
    juce::String altKey = "A";
    juce::String altScale = "Major";
    float altConfidence = 0.64f;

    // --- artist -----------------------------------------------------------
    juce::String currentNote = "C#4";
    float currentCents = 7.0f;
    int comfortableLowMidi = 48, comfortableHighMidi = 67;
    int strongLowMidi = 55, strongHighMidi = 64;
    int extendedLowMidi = 43, extendedHighMidi = 72;
    int falsettoHighMidi = 81;

    // --- fit --------------------------------------------------------------
    float artistFit = 0.89f;
    float rangeFit = 0.85f;
    float hookMatch = 0.91f;
    int recommendedTranspose = -2;
    juce::String newKey = "E";
    juce::String newScale = "Minor";
    float estimatedFit = 0.94f;

    // --- 808 / sample -----------------------------------------------------
    juce::String sampleNote = "G";
    int sampleRecommendedSemitones = -1;
    float sampleFineTuneCents = 4.0f;
    float sampleConfidence = 0.86f;

    // --- live vectors -----------------------------------------------------
    std::array<float, 12> chroma { 0.22f, 0.87f, 0.69f, 0.18f, 0.73f, 0.21f,
                                   1.0f,  0.76f, 0.70f, 0.19f, 0.17f, 0.31f };

    bool analyzing = false;   // ramps the wheel orbit speed

    // Scale membership of the detected key (F# natural minor by default:
    // F# G# A B C# D E -> pitch classes 6 8 9 11 1 2 4).
    std::array<bool, 12> scaleNotes { false, true, true, false, true, false,
                                      true,  false, true, true,  false, true };
    int rootNote = 6;   // F#
};

// Lock-free publish/read of immutable snapshots (09_JUCE_HANDOFF pattern).
class AnalysisDisplayModel
{
public:
    AnalysisDisplayModel()  { publish (std::make_shared<const AnalysisSnapshot>()); }

    void publish (std::shared_ptr<const AnalysisSnapshot> next)
    {
        std::atomic_store (&snapshot, std::move (next));
    }

    std::shared_ptr<const AnalysisSnapshot> get() const
    {
        return std::atomic_load (&snapshot);
    }

private:
    mutable std::shared_ptr<const AnalysisSnapshot> snapshot;
};

// -----------------------------------------------------------------------------
//  DemoFeed - message-thread generator of the moving placeholder data:
//  chroma shimmer, log-spectrum bands, the 12-second pitch trail, tuner
//  cents and a synthetic vocal note. Deterministic (seeded), so uishot
//  screenshots are reproducible.
// -----------------------------------------------------------------------------
struct DemoFeed
{
    static constexpr int spectrumBands = 96;
    static constexpr int trailSeconds  = 12;
    static constexpr int trailRate     = 45;                       // Hz
    static constexpr int trailLength   = trailSeconds * trailRate; // 540

    std::array<float, spectrumBands> spectrum {};
    std::array<float, spectrumBands> spectrumPeak {};

    // Pitch trail in MIDI note numbers; <= 0 marks an unvoiced gap.
    std::array<float, trailLength> trailMidi {};
    int trailHead = 0;

    std::array<float, 12> chroma {};
    float tunerCents  = 4.0f;
    float vocalCents  = 7.0f;
    float vocalMidi   = 61.0f;   // C#4
    double t = 0.0;
    double trailAccum = 0.0;

    DemoFeed()
    {
        trailMidi.fill (-1.0f);
        // Pre-fill the trail so the panel opens with 12 s of history.
        for (int i = 0; i < trailLength; ++i)
            advanceTrail (1.0 / trailRate);
    }

    void tick (double dt, const AnalysisSnapshot& base)
    {
        t += dt;

        // Chroma shimmer around the contract vector - each bin breathes at
        // its own rate so the wheel nodes pulse organically, never snapping.
        for (int i = 0; i < 12; ++i)
        {
            const float wobble = 0.10f * (float) std::sin (t * (1.3 + 0.37 * i) + i * 2.1);
            chroma[(size_t) i] = juce::jlimit (0.0f, 1.0f,
                                               base.chroma[(size_t) i] * (0.92f + wobble));
        }

        // Spectrum: harmonic peaks of F#1 (46.2 Hz) over a pink-ish bed,
        // pumping at the beat rate. Log frequency, 20 Hz .. 20 kHz.
        const double beat = std::fmod (t * base.bpm / 60.0, 1.0);
        const float pump  = (float) std::exp (-beat * 5.0);

        for (int b = 0; b < spectrumBands; ++b)
        {
            const double f = 20.0 * std::pow (1000.0, b / (double) (spectrumBands - 1));
            float level = 0.30f - 0.22f * (float) (std::log10 (f / 20.0) / 3.0);

            for (int h = 1; h <= 24; ++h)
            {
                const double fh = 46.25 * h;
                const double dist = std::abs (std::log2 (f / fh));
                if (dist < 0.05)
                    level += (0.55f / (float) std::sqrt ((double) h)) * (1.0f - (float) (dist / 0.05));
            }

            level *= 0.55f + 0.45f * pump;
            level += 0.04f * (float) std::sin (t * 7.0 + b * 1.7);
            level = juce::jlimit (0.0f, 1.0f, level);

            spectrum[(size_t) b] += 0.35f * (level - spectrum[(size_t) b]);
            spectrumPeak[(size_t) b] = juce::jmax (spectrumPeak[(size_t) b] - (float) dt * 0.4f,
                                                   spectrum[(size_t) b]);
        }

        // Vocal line + tuner drift.
        trailAccum += dt;
        while (trailAccum >= 1.0 / trailRate)
        {
            trailAccum -= 1.0 / trailRate;
            advanceTrail (1.0 / trailRate);
        }

        vocalCents = 7.0f + 3.5f * (float) std::sin (t * 0.9);
        tunerCents = 4.0f + 2.2f * (float) std::sin (t * 0.55) + 0.8f * (float) std::sin (t * 2.3);
    }

    juce::String currentNoteName() const
    {
        const int midi = juce::roundToInt (vocalMidi);
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

private:
    void advanceTrail (double dt)
    {
        phrase += dt;

        // A wandering melody around C#4 in F# minor with breath gaps,
        // shaped like the approved mockup's trail.
        static const float degrees[] = { 61.0f, 59.0f, 61.0f, 64.0f, 62.0f,
                                         61.0f, 57.0f, 59.0f, 61.0f, 66.0f };

        if (phrase > noteLen)
        {
            phrase = 0.0;
            noteLen = 0.35 + 0.4 * seededNoise();
            if (breathCountdown-- <= 0)
            {
                inBreath = true;
                breathCountdown = 4 + (int) (seededNoise() * 4.0);
                noteLen = 0.22;
            }
            else
            {
                inBreath = false;
                noteIndex = (noteIndex + 1 + (int) (seededNoise() * 3.0)) % 10;
            }
        }

        if (inBreath)
        {
            trailMidi[(size_t) trailHead] = -1.0f;
        }
        else
        {
            // Slow glide plus micro-drift so the trail reads as a sung line,
            // not a step sequencer.
            const float target = degrees[noteIndex];
            vocalMidi += 0.075f * (target - vocalMidi);
            const float vibrato = 0.14f * (float) std::sin (vibPhase += dt * 34.0);
            const float drift = 0.10f * (float) (seededNoise() - 0.5);
            trailMidi[(size_t) trailHead] = vocalMidi + vibrato + drift;
        }
        trailHead = (trailHead + 1) % trailLength;
    }

    double seededNoise()
    {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double) ((seed >> 33) & 0x7fffffff) / (double) 0x7fffffff;
    }

    double phrase = 0.0, noteLen = 0.5, vibPhase = 0.0;
    int noteIndex = 0, breathCountdown = 5;
    bool inBreath = false;
    juce::uint64 seed = 0x4b47;
};

} // namespace keyglo
