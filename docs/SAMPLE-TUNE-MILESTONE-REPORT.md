# KeyGlo — 808/Sample Tune & Preview DSP Report (Milestone 4)

The last engine milestone: real 808/sample tuning and audible preview DSP.
171 checks, 0 failed. Every panel in the plugin now runs on real analysis.

## What became real

- **Sample pitch detection** (`Source/analysis/SamplePitchDetector`): finds
  the transient, skips the attack (an 808's pitch-drop click), analyses the
  SUSTAIN with the pitch tracker decimated to 8 kHz (floor ~25 Hz — real
  808 territory), and takes robust median statistics. Early-vs-late sustain
  drift beyond 60 cents is reported as **PITCH ENVELOPE: C1 > G1** with both
  notes, instead of pretending the sample has one note; noise bursts get
  "NO STABLE NOTE".
- **Drop a sample on the 808 panel**: real min/max waveform with the file
  name, detected sustain note, the tuner needle showing the sample's actual
  cents deviation, and a recommendation toward the nearest tone of the
  detected beat scale (nearest exact semitone when no key stands). Near-tie
  targets resolve DOWN — producers tune 808s down.
- **SOLO** auditions the *tuned* sample on loop through the plugin's output,
  via a lock-free double-slot player (worker renders, audio thread reads;
  slots never freed, no locks, click-free blend in and out).
- **APPLY TUNE** renders `<name> (KeyGlo tuned).wav` beside the source,
  repitched by the exact fractional shift (Lagrange resampling — the
  one-shot workflow where length change is expected).
- **Preview pitch shifter** (`Source/dsp/PreviewPitchShifter`): real-time
  two-tap granular delay-line shifter behind Transpose Preview — B/A is a
  smoothed loudness-matched blend, Preview Mix is a real dry/wet, Fine Tune
  cents feed the ratio. The dry path is delayed by the half-grain the wet
  path needs and that latency is **reported to the host and never changes**
  (engaged, disengaged or bypassed).

## The engagement rule (a real product decision)

parameters.json ships non-neutral defaults: B selected, −2 st, 40 % mix. A
fresh analyzer instance must never pitch-shift the program on its own, so
the preview only arms once a real key result exists (an atomic mirror of
"beat result stands" that the audio thread can read). The gain tests caught
this: every level came out +3 dB hot because the default state was mixing a
shifted copy into the program.

## Bugs the tests caught this milestone

1. **Default-state shifting** (above) — found by 3 dB errors in the
   milestone-2 gain checks, pinned by a fresh-instance disarm test.
2. **Crossfade gain candidates, measured not argued**: signed sin/cos
   equal-power *clicks* (polarity flips −1→+1 at the phase wrap, 0.708
   sample step); |cos| *combs* tone-dependently (a persistent signed
   cross-term cost −3.1 dB on a pure sine, matching the math exactly).
   Shipped: Hann amplitude-complementary with sqrt(4/3) makeup — constant
   average loss, compensated exactly, ±1.3 dB pure-tone extremes.
3. **The fixture clipped in 16-bit**: chords + bass drone summed past full
   scale, and the clipped harmonics wrecked the in-scale gate the fixture
   exists to exercise. Fixtures now normalise to 0.7 peak.
4. **An unflushed WAV writer**: the beat-context file was analysed before
   the writer's destructor finalised the header, so the reader saw an
   empty file and the beat published "unreliable" — while the detector
   passed the same audio directly. The gate diagnostics named the real
   culprit. WAV writers are now scoped, with a comment telling the story.
5. **A wrong test expectation**: a G2 sample 30 cents SHARP is nearer G#2
   (+0.70) than F#2 (−1.30); the engine was right and the test expected the
   mockup's in-tune-G2 scenario. Both cases are now tested separately.

## The money test

Drop a G2 808 that is 30 cents sharp onto a detected F# minor beat, Apply
Tune, then re-analyse the rendered file with no key context at all: it
reads **G#2 within half a cent**. The output of the tuning feature is
verified by the analysis feature.

## Verification (171 checks, 0 failed)

New this milestone: 808 pitch within 0.12 semitones at F#1; deviation
measurement at +30 cents; scale-aware recommendation (G2 vs F# minor →
−1 st); glide → pitch envelope; noise → no stable note; shifter latency
verified by impulse arrival; 220 Hz +12 st → 440 Hz dominant at neutral
loudness; click-free engagement; fresh-instance disarm; and the money test
plus SOLO playing the tuned sample over silence and returning cleanly.

## What remains before 1.0 (no engine work left)

- Preset persistence + Save/Undo/Redo commands (header buttons are stubs).
- MIDI scale export from COPY SCALE (text copy already works).
- Universal build, installer, notarisation — the MasterGlo/SourceGlo
  release path, ported and waiting.
- A human listen in a real DAW session.
