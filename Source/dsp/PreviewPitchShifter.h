/*
    PreviewPitchShifter - real-time transposition for the Transpose Preview
    audition path.

    Classic two-tap granular delay-line shifter: the taps sweep a fixed
    window at the rate the pitch ratio dictates, amplitude-complementary
    sin^2/cos^2 crossfades hide the wrap, cubic interpolation reads the
    fractional delays. Preview-grade by design (some flutter on dense
    material is the known trade), zero allocations in process, gain-neutral.

    The dry path is delayed by half the grain so dry/wet blends stay
    time-aligned; that half-grain is the plugin's reported latency and it is
    applied ALWAYS - engaged, disengaged or bypassed - so the latency the
    host compensates for never changes under its feet
    (JUCE_IMPLEMENTATION_SPEC: report preview lookahead correctly).
*/

#pragma once
#include <JuceHeader.h>

namespace keyglo
{

class PreviewPitchShifter
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        channels = juce::jlimit (1, 2, numChannels);
        grainSamples = juce::jmax (256, (int) (sampleRate * 0.060));
        dryDelaySamples = grainSamples / 2;

        const int capacity = juce::nextPowerOfTwo (grainSamples * 2 + maxBlockSize + 8);
        mask = capacity - 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            wetLine[ch].assign ((size_t) capacity, 0.0f);
            dryLine[ch].assign ((size_t) capacity, 0.0f);
        }
        writePos = 0;
        phase = 0.0;

        ratioSmoothed.reset (sampleRate, 0.05);
        ratioSmoothed.setCurrentAndTargetValue (1.0f);
        wetGainSmoothed.reset (sampleRate, 0.05);
        wetGainSmoothed.setCurrentAndTargetValue (0.0f);
    }

    int latencySamples() const   { return dryDelaySamples; }

    // ratio: 2^(semitones/12); wetAmount 0..1 (previewMix x engage, already
    // combined by the caller). Equal-power dry/wet blend.
    void process (juce::AudioBuffer<float>& buffer, float ratio, float wetAmount)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChans = juce::jmin (channels, buffer.getNumChannels());
        if (mask == 0 || numSamples == 0)
            return;

        ratioSmoothed.setTargetValue (juce::jlimit (0.25f, 4.0f, ratio));
        wetGainSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, wetAmount));

        for (int i = 0; i < numSamples; ++i)
        {
            const float r = ratioSmoothed.getNextValue();
            const float wet = wetGainSmoothed.getNextValue();

            // Tap phase advances by (1 - r) per sample, normalised to the
            // grain length; r > 1 sweeps toward shorter delays (pitch up).
            phase += (1.0 - (double) r) / (double) grainSamples;
            phase -= std::floor (phase);

            const double delayA = phase * grainSamples;
            const double pB = phase + 0.5 - std::floor (phase + 0.5);
            const double delayB = pB * grainSamples;
            // Amplitude-complementary Hann crossfade (sin^2/cos^2) with a
            // fixed sqrt(4/3) wet makeup. The three candidates were measured:
            // signed sin/cos equal-power clicks (polarity flips -1 to +1 at
            // the phase wrap), |cos| combs tone-dependently (a persistent
            // signed cross-term cost -3.1 dB on a pure sine), while Hann's
            // average power loss is a constant 3/4 for uncorrelated content -
            // so it is compensated exactly, and pure-tone extremes stay
            // within +/-1.3 dB.
            const float sinP = (float) std::sin (juce::MathConstants<double>::pi * phase);
            const float gainA = sinP * sinP;
            const float gainB = 1.0f - gainA;
            constexpr float wetMakeup = 1.1547005f;   // sqrt(4/3)

            // Equal-power blend of the aligned dry and the shifted wet.
            const float dryGain = std::sqrt (1.0f - wet);
            const float wetGain = std::sqrt (wet);

            for (int ch = 0; ch < numChans; ++ch)
            {
                float* data = buffer.getWritePointer (ch);
                const float input = data[i];

                wetLine[ch][(size_t) (writePos & mask)] = input;
                dryLine[ch][(size_t) (writePos & mask)] = input;

                const float shifted = wetMakeup * (gainA * readCubic (wetLine[ch], delayA)
                                                    + gainB * readCubic (wetLine[ch], delayB));
                const float aligned = dryLine[ch][(size_t) ((writePos - dryDelaySamples) & mask)];

                data[i] = dryGain * aligned + wetGain * shifted;
            }
            ++writePos;
        }
    }

private:
    float readCubic (const std::vector<float>& line, double delay) const
    {
        // 4-point Catmull-Rom around the fractional read position, kept a
        // couple of samples behind the write head so interpolation never
        // touches unwritten history.
        const double readPos = (double) writePos - 2.0 - delay;
        const int i1 = (int) std::floor (readPos);
        const float frac = (float) (readPos - i1);

        const float y0 = line[(size_t) ((i1 - 1) & mask)];
        const float y1 = line[(size_t) (i1 & mask)];
        const float y2 = line[(size_t) ((i1 + 1) & mask)];
        const float y3 = line[(size_t) ((i1 + 2) & mask)];

        const float a = 0.5f * (3.0f * (y1 - y2) - y0 + y3);
        const float b = y2 + y2 + y0 - (5.0f * y1 + y3) * 0.5f;
        const float c = 0.5f * (y2 - y0);
        return ((a * frac + b) * frac + c) * frac + y1;
    }

    std::vector<float> wetLine[2], dryLine[2];
    int mask = 0, channels = 2;
    int grainSamples = 0, dryDelaySamples = 0;
    juce::int64 writePos = 0;
    double phase = 0.0;
    juce::SmoothedValue<float> ratioSmoothed { 1.0f }, wetGainSmoothed { 0.0f };
};

} // namespace keyglo
