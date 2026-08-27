/*
    AnalysisCoordinator - the worker thread between the audio path and the UI.

    Jobs: analyse a dropped audio file (decode + key + tempo), analyse the
    live capture ring, and the continuous fast lane (input spectrum + chroma
    for the wheel pulse at ~20 Hz). Results publish as immutable snapshots
    through AnalysisDisplayModel; nothing here ever touches the audio thread.

    Live sessions re-analyse the ring automatically every few seconds while
    audio is flowing; host tempo (when the DAW provides one) overrides the
    detected BPM per the spec ("host BPM for live sessions").
*/

#pragma once
#include <JuceHeader.h>
#include "../AnalysisModel.h"
#include "CaptureRing.h"
#include "BeatKeyDetector.h"
#include "TempoDetector.h"

namespace keyglo
{

class AnalysisCoordinator : private juce::Thread
{
public:
    AnalysisCoordinator (CaptureRing& ringIn, AnalysisDisplayModel& modelIn)
        : juce::Thread ("KeyGlo Analysis"), ring (ringIn), model (modelIn)
    {
        formats.registerBasicFormats();
        startThread (juce::Thread::Priority::low);
    }

    ~AnalysisCoordinator() override
    {
        stopThread (4000);
    }

    void analyseFileAsync (const juce::File& file)
    {
        {
            const juce::ScopedLock sl (jobLock);
            pendingFile = file;
            fileRequested = true;
        }
        notify();
    }

    void analyseRingNow()
    {
        ringRequested.store (true);
        notify();
    }

    bool isBusy() const                 { return busy.load(); }

    // Host transport info, stored from processBlock via atomics.
    void setHostTempo (double bpm, bool playing)
    {
        hostBpm.store ((float) bpm);
        hostPlaying.store (playing);
    }

private:
    void run() override
    {
        juce::int64 lastAutoAnalyse = 0;
        juce::int64 lastSeen = 0;

        while (! threadShouldExit())
        {
            // --- fast lane: live spectrum + chroma at ~20 Hz ---------------
            publishLiveVisuals();

            // --- explicit jobs ---------------------------------------------
            juce::File fileJob;
            {
                const juce::ScopedLock sl (jobLock);
                if (fileRequested)
                {
                    fileJob = pendingFile;
                    fileRequested = false;
                }
            }

            if (fileJob != juce::File())
                analyseFile (fileJob);
            else if (ringRequested.exchange (false))
                analyseRing (true);
            else
            {
                // --- periodic live re-analysis -----------------------------
                const auto written = ring.samplesWritten();
                const auto now = juce::Time::currentTimeMillis();
                const bool freshAudio = written > lastSeen + (juce::int64) (ring.sampleRate() * 2.0);
                if (freshAudio && now - lastAutoAnalyse > 3000
                     && written > (juce::int64) (ring.sampleRate() * 6.0))
                {
                    lastAutoAnalyse = now;
                    lastSeen = written;
                    analyseRing (false);
                }
            }

            wait (50);
        }
    }

    void publishLiveVisuals()
    {
        constexpr int fftOrder = 13;              // 8192
        constexpr int fftSize = 1 << fftOrder;

        float channelRms = 0.0f;
        const int got = ring.readLatestMono (liveBuffer, fftSize, channelRms);
        auto live = std::make_shared<LiveVisuals>();

        if (got >= fftSize / 2 && channelRms > 1.0e-5f)
        {
            static thread_local juce::dsp::FFT fft (fftOrder);
            liveFft.resize ((size_t) fftSize * 2);
            std::fill (liveFft.begin(), liveFft.end(), 0.0f);
            for (int i = 0; i < got; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) (got - 1));
                liveFft[(size_t) i] = liveBuffer[(size_t) i] * w;
            }
            fft.performFrequencyOnlyForwardTransform (liveFft.data());

            const double binHz = ring.sampleRate() / (double) fftSize;

            // Log-frequency bands, 20 Hz .. 20 kHz.
            for (int band = 0; band < 96; ++band)
            {
                const double f0 = 20.0 * std::pow (1000.0, band / 95.0);
                const double f1 = 20.0 * std::pow (1000.0, (band + 1) / 95.0);
                const int b0 = juce::jmax (1, (int) (f0 / binHz));
                const int b1 = juce::jmin (fftSize / 2 - 1, juce::jmax (b0 + 1, (int) (f1 / binHz)));
                float peak = 0.0f;
                for (int b = b0; b < b1; ++b)
                    peak = juce::jmax (peak, liveFft[(size_t) b]);
                // Perceptual-ish scaling into 0..1.
                live->spectrum[(size_t) band] =
                    juce::jlimit (0.0f, 1.0f,
                                  (juce::Decibels::gainToDecibels (peak / (float) fftSize * 8.0f,
                                                                   -72.0f) + 72.0f) / 66.0f);
            }

            // Coarse live chroma from spectral peaks (wheel pulse drive).
            std::array<float, 12> chroma {};
            const int loBin = juce::jmax (2, (int) (40.0 / binHz));
            const int hiBin = juce::jmin (fftSize / 2 - 2, (int) (4000.0 / binHz));
            for (int b = loBin; b < hiBin; ++b)
            {
                const float m = liveFft[(size_t) b];
                if (m <= liveFft[(size_t) b - 1] || m < liveFft[(size_t) b + 1])
                    continue;
                const double freq = b * binHz;
                const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
                const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
                chroma[(size_t) pc] += std::pow (m, 0.6f) * (freq < 250.0 ? 2.2f : 1.0f);
            }
            float maxC = 0.0f;
            for (auto v : chroma) maxC = juce::jmax (maxC, v);
            if (maxC > 0.0f)
                for (int i = 0; i < 12; ++i)
                    live->chroma[(size_t) i] = chroma[(size_t) i] / maxC;

            live->inputRms = channelRms;
            live->active = true;
        }

        model.publishLive (std::move (live));
    }

    void analyseFile (const juce::File& file)
    {
        busy.store (true);
        publishAnalyzing (file.getFileName());

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 1024)
        {
            publishFailure (file.getFileName());
            busy.store (false);
            return;
        }

        const int numSamples = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                              (juce::int64) (reader->sampleRate * 30.0));
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, numSamples);
        reader->read (&buffer, 0, numSamples, 0, true, true);

        std::vector<float> mono ((size_t) numSamples, 0.0f);
        for (int ch = 0; ch < (int) reader->numChannels; ++ch)
        {
            const float* src = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                mono[(size_t) i] += src[i] / (float) reader->numChannels;
        }

        const auto keyResult = BeatKeyDetector::analyse (mono.data(), numSamples, reader->sampleRate);
        const auto tempoResult = TempoDetector::analyse (mono.data(), numSamples, reader->sampleRate);
        publishResult (keyResult, tempoResult, file.getFileName(), false);
        busy.store (false);
    }

    void analyseRing (bool explicitRequest)
    {
        busy.store (true);
        if (explicitRequest)
            publishAnalyzing ("LIVE INPUT");

        float channelRms = 0.0f;
        const int wanted = (int) (ring.sampleRate() * CaptureRing::seconds);
        const int got = ring.readLatestMono (ringBuffer, wanted, channelRms);

        if (got > (int) (ring.sampleRate() * 4.0) && channelRms > 1.0e-4f)
        {
            const auto keyResult = BeatKeyDetector::analyse (ringBuffer.data(), got, ring.sampleRate());
            const auto tempoResult = TempoDetector::analyse (ringBuffer.data(), got, ring.sampleRate());
            publishResult (keyResult, tempoResult, "LIVE INPUT", true);
        }
        else if (explicitRequest)
        {
            publishFailure ("LIVE INPUT");
        }
        busy.store (false);
    }

    void publishAnalyzing (const juce::String& source)
    {
        auto next = std::make_shared<AnalysisSnapshot> (*model.get());
        next->analyzing = true;
        next->sourceName = source;
        model.publish (std::move (next));
    }

    void publishFailure (const juce::String& source)
    {
        auto next = std::make_shared<AnalysisSnapshot> (*model.get());
        next->analyzing = false;
        next->hasBeatResult = true;
        next->noReliableKey = true;
        next->sourceName = source;
        model.publish (std::move (next));
    }

    void publishResult (const BeatKeyResult& keyResult, const TempoResult& tempoResult,
                        const juce::String& source, bool liveSession)
    {
        auto next = std::make_shared<AnalysisSnapshot> (*model.get());
        next->analyzing = false;
        next->hasBeatResult = true;
        next->sourceName = source;
        next->chroma = keyResult.chroma;

        next->noReliableKey = ! keyResult.reliable;
        if (keyResult.reliable)
        {
            next->setKeyFromPitchClass (keyResult.rootPc, keyResult.minor);
            next->keyConfidence = keyResult.confidence;
            next->tuningCents = keyResult.tuningCents;

            if (! keyResult.alternatives.empty())
            {
                const auto& alt = keyResult.alternatives.front();
                BeatKeyResult altName;
                altName.rootPc = alt.rootPc;
                next->altKey = altName.keyName();
                next->altScale = alt.minor ? "Minor" : "Major";
                next->altConfidence = alt.confidence;
            }
        }

        // Host tempo wins in a live session; detected BPM otherwise.
        const float host = hostBpm.load();
        if (liveSession && host > 20.0f && host < 400.0f)
        {
            next->bpm = host;
            next->bpmSource = "HOST";
        }
        else if (tempoResult.reliable)
        {
            next->bpm = tempoResult.bpm;
            next->bpmSource = liveSession ? "LIVE" : "FILE";
        }
        else
        {
            next->bpm = 0.0f;      // shown as "--"
            next->bpmSource = "";
        }

        model.publish (std::move (next));
    }

    CaptureRing& ring;
    AnalysisDisplayModel& model;
    juce::AudioFormatManager formats;

    juce::CriticalSection jobLock;
    juce::File pendingFile;
    bool fileRequested = false;
    std::atomic<bool> ringRequested { false };
    std::atomic<bool> busy { false };
    std::atomic<float> hostBpm { 0.0f };
    std::atomic<bool> hostPlaying { false };

    std::vector<float> liveBuffer, liveFft, ringBuffer;
};

} // namespace keyglo
