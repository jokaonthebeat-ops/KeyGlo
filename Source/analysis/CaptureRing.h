/*
    CaptureRing - lock-free rolling capture of the plugin's input for the
    analysis engine. Single writer (audio thread), snapshot readers (worker).

    The audio thread only memcpies into preallocated storage and bumps an
    atomic; the worker copies out the most recent N seconds. Capacity is
    allocated in prepareToPlay, never on the audio thread.

    Silence gating is per-channel energy, never a mono sum - perfectly
    out-of-phase stereo cancels to "silence" and is exactly the material an
    analyzer must not skip (SourceGlo lesson).
*/

#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace keyglo
{

class CaptureRing
{
public:
    static constexpr double seconds = 12.0;

    void prepare (double sampleRate, int numChannels)
    {
        const juce::SpinLock::ScopedLockType sl (resizeLock);
        rate = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        capacity = juce::nextPowerOfTwo ((int) std::ceil (sampleRate * seconds));
        for (int ch = 0; ch < 2; ++ch)
            storage[ch].assign ((size_t) capacity, 0.0f);
        writePos.store (0, std::memory_order_release);
        totalWritten.store (0, std::memory_order_release);
    }

    // Audio thread. No locks (the spin lock is only contended during
    // prepareToPlay, when the host is not calling processBlock).
    void write (const juce::AudioBuffer<float>& buffer)
    {
        if (capacity == 0 || ! resizeLock.tryEnter())
            return;

        const int n = buffer.getNumSamples();
        int pos = writePos.load (std::memory_order_relaxed);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = buffer.getReadPointer (juce::jmin (ch, buffer.getNumChannels() - 1));
            float* dst = storage[ch].data();
            int p = pos;
            for (int i = 0; i < n; ++i)
            {
                dst[p] = src[i];
                p = (p + 1) & (capacity - 1);
            }
        }

        writePos.store ((pos + n) & (capacity - 1), std::memory_order_release);
        totalWritten.fetch_add ((juce::int64) n, std::memory_order_release);
        resizeLock.exit();
    }

    // Worker thread: copy out the newest `numSamples` as mono-summed +
    // per-channel peak info. Returns samples actually available.
    int readLatestMono (std::vector<float>& dest, int numSamples,
                        float& maxChannelRms) const
    {
        const juce::SpinLock::ScopedLockType sl (resizeLock);
        if (capacity == 0)
            return 0;

        const juce::int64 written = totalWritten.load (std::memory_order_acquire);
        const int available = (int) juce::jmin ((juce::int64) capacity - 1, written);
        const int n = juce::jmin (numSamples, available);
        if (n <= 0)
            return 0;

        dest.resize ((size_t) n);
        const int end = writePos.load (std::memory_order_acquire);
        int start = (end - n) & (capacity - 1);

        double sumSq[2] = { 0.0, 0.0 };
        for (int i = 0; i < n; ++i)
        {
            const int p = (start + i) & (capacity - 1);
            float mono = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float v = storage[ch][(size_t) p];
                mono += v;
                sumSq[ch] += (double) v * v;
            }
            dest[(size_t) i] = mono / (float) channels;
        }

        maxChannelRms = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            maxChannelRms = juce::jmax (maxChannelRms,
                                        (float) std::sqrt (sumSq[ch] / n));
        return n;
    }

    double sampleRate() const       { return rate; }
    juce::int64 samplesWritten() const { return totalWritten.load (std::memory_order_acquire); }

private:
    mutable juce::SpinLock resizeLock;
    std::vector<float> storage[2];
    int capacity = 0, channels = 2;
    double rate = 48000.0;
    std::atomic<int> writePos { 0 };
    std::atomic<juce::int64> totalWritten { 0 };
};

} // namespace keyglo
