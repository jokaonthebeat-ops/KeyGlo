# DSP and Analysis Architecture

## Beat key and scale

1. Convert input to a mono analysis stream without changing the audible path.
2. Estimate tuning offset and compensate the frequency-to-pitch-class mapping.
3. Use overlapping FFT frames and derive a harmonic pitch-class profile or HPCP/chroma vector.
4. Weight stable harmonic content more heavily than transients and broadband noise.
5. Accumulate over a useful window, then compare against major/minor templates and cadence/root evidence.
6. Return a ranked result list with confidence and alternatives. Display low confidence honestly.

Sparse percussion-only material may not have a meaningful key. Return `No Reliable Key` rather than inventing one.

## Vocal pitch and range

Use a monophonic tracker such as YIN or MPM. Reject frames below a voicing/confidence threshold. Smooth short jitter without flattening intentional slides.

During a range test, collect stable notes and classify:

- extended low/high: near the reliable extremes;
- comfortable range: central stable percentiles;
- strong zone: the densest, most stable area;
- falsetto: optional separately declared/recorded high register.

Do not diagnose medical strain. Use wording such as `near top of comfort zone` rather than claiming vocal damage.

## Hook fit score

Build a note-duration histogram from the hook. For each candidate transpose, shift the histogram and score:

- time inside strong zone;
- time inside comfortable zone;
- time inside extended range;
- sustained notes near/outside limits;
- distance of the median melody pitch from the strong-zone center;
- scale compatibility with the detected beat.

Normalize to 0–100 and show the best few choices. Allow the producer/artist to choose a different creative option.

## 808 and sample tuning

Detect the transient, then analyze the later sustain region. Use median/robust pitch statistics. If the pitch glides substantially, report the start and sustain notes or flag `Pitch Envelope Detected`.

## BPM

For a dropped full beat/loop, use onset strength and autocorrelation/tempo candidates. Show half-time and double-time possibilities when confidence is ambiguous. For live insert use, read the host BPM.

## Threading

- Real-time process block: buffering and inexpensive scalar meters only.
- Worker thread: file decoding, FFT batches, BPM, full-result calculations, profile/export operations.
- Message thread: display interpolation and user interaction.
