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
#include "VocalEngine.h"
#include "SamplePitchDetector.h"
#include "../state/ArtistProfileStore.h"
#include "../dsp/SamplePreviewPlayer.h"

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

    // Automatic re-analysis of the live ring. On by default; the demo film
    // holds it off until the beat is dropped on camera so that music can play
    // under the opening titles without a live verdict appearing before the
    // drop. A dropped FILE result already suppresses it permanently.
    void setAutoAnalysisEnabled (bool shouldRun)  { autoAnalysis.store (shouldRun); }

    // --- 808 / sample side (milestone 4) ----------------------------------
    void setPreviewPlayer (SamplePreviewPlayer* p)   { player = p; }

    void analyseSampleAsync (const juce::File& file)
    {
        {
            const juce::ScopedLock sl (jobLock);
            pendingSample = file;
            sampleRequested = true;
        }
        notify();
    }

    // Renders "<name> (KeyGlo tuned).wav" beside the analysed sample.
    void applyTuneAsync()
    {
        applyRequested.store (true);
        notify();
    }

    // --- vocal side (milestone 3) -----------------------------------------
    VocalEngine& vocals()               { return vocal; }

    void startRangeTest()               { vocal.startRangeTest(); publishVocalState(); }
    void advanceRangeTest()             { vocal.advanceRangeTest(); publishVocalState(); }
    void cancelRangeTest()              { vocal.cancelRangeTest(); publishVocalState(); }

    bool saveProfile (const juce::String& name)
    {
        if (! vocal.hasProfile())
            return false;
        const bool ok = ArtistProfileStore::save (name, vocal.getProfile());
        if (ok)
        {
            profileName = name;
            publishVocalState();
        }
        return ok;
    }

    bool loadProfile (const juce::String& name)
    {
        ArtistProfile p;
        if (! ArtistProfileStore::load (name, p))
            return false;
        vocal.setProfile (p);
        profileName = name;
        publishVocalState();
        return true;
    }

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
            // --- vocal lane: pitch-track whatever is new in the ring -------
            trackNewAudio();

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

            juce::File sampleJob;
            {
                const juce::ScopedLock sl (jobLock);
                if (sampleRequested)
                {
                    sampleJob = pendingSample;
                    sampleRequested = false;
                }
            }

            if (fileJob != juce::File())
                analyseFile (fileJob);
            else if (sampleJob != juce::File())
                analyseSample (sampleJob);
            else if (applyRequested.exchange (false))
                renderAppliedTune();
            else if (ringRequested.exchange (false))
                analyseRing (true);
            else
            {
                // --- periodic live re-analysis -----------------------------
                // Never automatic once a FILE result stands: the artist sings
                // over that beat, and re-analysing the vocal take would
                // silently replace the beat's key with the singer's. The
                // refresh button still forces a live pass.
                const bool fileResultStands = model.get()->sourceName.isNotEmpty()
                                                && model.get()->sourceName != "LIVE INPUT";
                const auto written = ring.samplesWritten();
                const auto now = juce::Time::currentTimeMillis();
                const bool freshAudio = written > lastSeen + (juce::int64) (ring.sampleRate() * 2.0);
                if (autoAnalysis.load() && ! fileResultStands && freshAudio
                     && now - lastAutoAnalyse > 3000
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

    // Pitch-track exactly the audio that arrived since the last pass, so the
    // trail advances in real time without re-analysing history.
    void trackNewAudio()
    {
        const auto written = ring.samplesWritten();
        if (vocalCursor == 0)
            vocalCursor = written;                     // start from "now"

        auto pending = written - vocalCursor;
        if (pending <= 0)
            return;

        // Cap catch-up so a long stall cannot stampede the worker.
        const auto maxChunk = (juce::int64) (ring.sampleRate() * 1.0);
        if (pending > maxChunk)
        {
            vocalCursor = written - maxChunk;
            pending = maxChunk;
        }

        // Needs at least one decimated frame's worth of samples.
        const int factor = PitchTracker::decimationFactor (ring.sampleRate());
        const int minSamples = PitchTracker::frameSize * factor;
        if (pending < minSamples)
            return;

        float channelRms = 0.0f;
        const int got = ring.readLatestMono (vocalBuffer, (int) pending + minSamples, channelRms);
        if (got < minSamples)
            return;

        const auto latest = vocal.processChunk (vocalBuffer.data(), got, ring.sampleRate());
        vocalCursor = written;

        latestVoiced.store (latest.voiced);
        if (latest.voiced)
        {
            latestMidi.store (latest.midi);
            latestCents.store (latest.cents);
        }

        // Recompute fit periodically while a profile exists and someone sings.
        const auto now = juce::Time::currentTimeMillis();
        if (vocal.hasProfile() && now - lastFitMs > 400)
        {
            lastFitMs = now;
            publishFit();
        }
    }

    void publishFit()
    {
        auto current = model.get();
        const bool haveScale = current->hasBeatResult && ! current->noReliableKey;
        const auto fit = vocal.isHookArmed()
                           ? vocal.scoreHook (current->scaleNotes, haveScale)
                           : vocal.scoreRecent (current->scaleNotes, haveScale);
        if (! fit.valid)
            return;

        auto next = std::make_shared<AnalysisSnapshot> (*current);
        next->hasFitResult = true;
        // The engine may hold a profile that never went through the range
        // test or the store (loaded directly); the snapshot must still say so
        // or SAVE PROFILE stays greyed out with a profile in hand.
        next->hasProfile = vocal.hasProfile();
        next->profileName = profileName;
        next->artistFit = fit.artistFit;
        next->rangeFit = fit.rangeFit;
        next->hookMatch = fit.hookMatch;
        next->recommendedTranspose = fit.recommendedTranspose;
        next->estimatedFit = fit.estimatedFit;
        next->fitByTranspose = fit.fitByTranspose;

        if (haveScale)
        {
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
            const int newRoot = ((next->rootNote + fit.recommendedTranspose) % 12 + 12) % 12;
            next->newKey = names[newRoot];
            next->newScale = next->scale;
        }
        model.publish (std::move (next));
    }

    // Profile/range-test state into the snapshot (called from the message
    // thread's control methods; publishing is lock-free).
    void publishVocalState()
    {
        auto next = std::make_shared<AnalysisSnapshot> (*model.get());
        next->hasProfile = vocal.hasProfile();
        next->profileName = profileName;
        next->rangeTestPhase = (int) vocal.phase();
        next->rangeTestFrames = vocal.phaseFrameCount();
        model.publish (std::move (next));
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

        // Vocal readouts ride the same publication.
        vocal.fillTrail (live->pitchTrail);
        live->voiced = latestVoiced.load();
        live->currentMidi = latestMidi.load();
        live->currentCents = latestCents.load();

        model.publishLive (std::move (live));

        // Range-test progress needs to reach the UI while a phase collects.
        if (vocal.phase() != RangeTestPhase::idle && vocal.phase() != RangeTestPhase::done)
        {
            auto current = model.get();
            const int frames = vocal.phaseFrameCount();
            if (current->rangeTestFrames != frames
                 || current->rangeTestPhase != (int) vocal.phase())
                publishVocalState();
        }
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

    // ------------------------------------------------------------------
    //  808 / sample pipeline
    // ------------------------------------------------------------------
    bool decodeMono (const juce::File& file, std::vector<float>& mono, double& rate)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 256)
            return false;

        const int numSamples = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                              (juce::int64) (reader->sampleRate * 20.0));
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, numSamples);
        reader->read (&buffer, 0, numSamples, 0, true, true);

        mono.assign ((size_t) numSamples, 0.0f);
        for (int ch = 0; ch < (int) reader->numChannels; ++ch)
        {
            const float* src = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                mono[(size_t) i] += src[i] / (float) reader->numChannels;
        }
        rate = reader->sampleRate;
        return true;
    }

    void analyseSample (const juce::File& file)
    {
        busy.store (true);

        double rate = 48000.0;
        if (! decodeMono (file, sampleAudio, rate))
        {
            auto next = std::make_shared<AnalysisSnapshot> (*model.get());
            next->hasSampleResult = true;
            next->sampleNoStableNote = true;
            next->sampleFileName = file.getFileName();
            next->sampleTunedReady = false;
            next->appliedTunePath = "";
            model.publish (std::move (next));
            busy.store (false);
            return;
        }
        sampleAudioRate = rate;
        sampleFile = file;

        auto current = model.get();
        const bool haveScale = current->hasBeatResult && ! current->noReliableKey;
        const auto tune = SamplePitchDetector::analyse (sampleAudio.data(),
                                                        (int) sampleAudio.size(), rate,
                                                        current->scaleNotes, haveScale);
        lastTune = tune;

        auto next = std::make_shared<AnalysisSnapshot> (*current);
        next->hasSampleResult = true;
        next->sampleFileName = file.getFileName();
        next->appliedTunePath = "";
        SamplePitchDetector::envelope<240> (sampleAudio.data(),
                                            (int) sampleAudio.size(), next->sampleWaveform);

        if (tune.valid)
        {
            next->sampleNoStableNote = false;
            next->samplePitchEnvelope = tune.pitchEnvelope;
            next->sampleNote = tune.noteName();
            next->sampleStartNote = tune.pitchEnvelope ? tune.startNoteName() : juce::String();
            next->sampleRecommendedSemitones = tune.recommendedSemitones;
            next->sampleFineTuneCents = tune.fineTuneCents;
            next->sampleDeviationCents = tune.deviationCents;
            next->sampleConfidence = tune.confidence;

            // Tuned audition buffer for SOLO.
            next->sampleTunedReady = renderTunedBuffer (tune.totalShiftSemitones, rate);
        }
        else
        {
            next->sampleNoStableNote = true;
            next->samplePitchEnvelope = false;
            next->sampleTunedReady = false;
            if (player != nullptr)
                player->clear();
        }

        model.publish (std::move (next));
        busy.store (false);
    }

    // Repitch by resampling (the one-shot workflow: length changes, the
    // texture stays honest) through a Lagrange interpolator.
    static void repitch (const std::vector<float>& in, double shiftSemitones,
                         juce::AudioBuffer<float>& out)
    {
        const double speed = std::pow (2.0, shiftSemitones / 12.0);
        const int outLength = juce::jmax (16, (int) ((double) in.size() / speed));
        out.setSize (1, outLength);

        juce::LagrangeInterpolator interp;
        interp.reset();
        interp.process (speed, in.data(), out.getWritePointer (0), outLength,
                        (int) in.size(), 0);
    }

    bool renderTunedBuffer (float shiftSemitones, double rate)
    {
        if (player == nullptr || sampleAudio.empty())
            return false;
        juce::AudioBuffer<float> tuned;
        repitch (sampleAudio, shiftSemitones, tuned);
        player->install (tuned, rate);
        return true;
    }

    void renderAppliedTune()
    {
        if (! lastTune.valid || sampleAudio.empty() || sampleFile == juce::File())
            return;

        busy.store (true);
        juce::AudioBuffer<float> tuned;
        repitch (sampleAudio, lastTune.totalShiftSemitones, tuned);

        auto out = sampleFile.getSiblingFile (
            sampleFile.getFileNameWithoutExtension() + " (KeyGlo tuned).wav");
        out.deleteFile();

        bool ok = false;
        {
            juce::WavAudioFormat wav;
            auto stream = out.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (
                stream != nullptr ? wav.createWriterFor (stream.get(), sampleAudioRate,
                                                         1, 24, {}, 0)
                                  : nullptr);
            if (writer != nullptr)
            {
                stream.release();
                ok = writer->writeFromAudioSampleBuffer (tuned, 0, tuned.getNumSamples());
            }
        }

        auto next = std::make_shared<AnalysisSnapshot> (*model.get());
        next->appliedTunePath = ok ? out.getFullPathName() : juce::String();
        model.publish (std::move (next));
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

        // A fit scored before any key existed carries hookMatch = 0, which
        // reads as "terrible match" rather than "not known yet". Now that a
        // key stands, re-score so the pod tells the truth.
        if (vocal.hasProfile())
        {
            lastFitMs = juce::Time::currentTimeMillis();
            publishFit();
        }
    }

    CaptureRing& ring;
    AnalysisDisplayModel& model;
    juce::AudioFormatManager formats;

    juce::CriticalSection jobLock;
    juce::File pendingFile, pendingSample;
    bool fileRequested = false, sampleRequested = false;
    std::atomic<bool> ringRequested { false }, applyRequested { false };
    std::atomic<bool> autoAnalysis { true };

    SamplePreviewPlayer* player = nullptr;
    std::vector<float> sampleAudio;
    double sampleAudioRate = 48000.0;
    juce::File sampleFile;
    SampleTuneResult lastTune;
    std::atomic<bool> busy { false };
    std::atomic<float> hostBpm { 0.0f };
    std::atomic<bool> hostPlaying { false };

    std::vector<float> liveBuffer, liveFft, ringBuffer, vocalBuffer;

    VocalEngine vocal;
    juce::String profileName;
    juce::int64 vocalCursor = 0, lastFitMs = 0;
    std::atomic<bool> latestVoiced { false };
    std::atomic<float> latestMidi { 0.0f }, latestCents { 0.0f };
};

} // namespace keyglo
