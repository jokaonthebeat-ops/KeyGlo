/*
    SamplePreviewPlayer - lock-free audition of the TUNED sample for the 808
    panel's SOLO button.

    Double-slot handoff (the SourceGlo audition pattern): the worker renders
    the tuned sample into whichever slot is not active, then flips an atomic
    generation counter. The audio thread only ever reads the active slot;
    slots are never freed, so a reader caught mid-block on the old slot
    stays on valid memory and picks up the new one next block. No locks, no
    allocation on the audio thread.
*/

#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace keyglo
{

class SamplePreviewPlayer
{
public:
    // Worker side: install a rendered buffer. Never called concurrently
    // with itself (single worker).
    void install (const juce::AudioBuffer<float>& rendered, double rate)
    {
        const int next = 1 - activeSlot.load (std::memory_order_relaxed);
        slots[next].buffer.makeCopyOf (rendered);
        slots[next].sampleRate = rate;
        activeSlot.store (next, std::memory_order_release);
        cleared.store (false, std::memory_order_release);
        generation.fetch_add (1, std::memory_order_release);
    }

    void clear()
    {
        generation.fetch_add (1, std::memory_order_release);
        cleared.store (true, std::memory_order_release);
    }

    bool hasSample() const
    {
        return ! cleared.load (std::memory_order_acquire)
                 && generation.load (std::memory_order_acquire) > 0;
    }

    // Audio thread: overwrite the block with the looping tuned sample.
    // Returns false (leaves the buffer untouched) when nothing is loaded.
    bool render (juce::AudioBuffer<float>& out, double hostRate)
    {
        if (! hasSample())
            return false;

        const int slot = activeSlot.load (std::memory_order_acquire);
        const auto& s = slots[slot];
        if (s.buffer.getNumSamples() < 16)
            return false;

        // Restart from the top whenever the worker swapped buffers.
        const auto gen = generation.load (std::memory_order_acquire);
        if (gen != playbackGeneration)
        {
            playbackGeneration = gen;
            position = 0.0;
        }

        const double step = s.sampleRate / juce::jmax (1.0, hostRate);
        const int total = s.buffer.getNumSamples();
        const int loopGap = (int) (s.sampleRate * 0.25);   // breath between hits

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            float value = 0.0f;
            const int index = (int) position;
            if (index < total - 1)
            {
                const float frac = (float) (position - index);
                const float a = s.buffer.getSample (0, index);
                const float b = s.buffer.getSample (0, index + 1);
                value = a + frac * (b - a);
            }

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.setSample (ch, i, value);

            position += step;
            if (position >= (double) (total + loopGap))
                position = 0.0;
        }
        return true;
    }

private:
    struct Slot
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 48000.0;
    };

    Slot slots[2];
    std::atomic<int> activeSlot { 0 };
    std::atomic<juce::uint32> generation { 0 };
    std::atomic<bool> cleared { false };

    // Audio-thread locals.
    double position = 0.0;
    juce::uint32 playbackGeneration = 0;
};

} // namespace keyglo
