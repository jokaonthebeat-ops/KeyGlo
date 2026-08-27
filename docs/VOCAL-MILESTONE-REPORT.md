# KeyGlo — Vocal Range & Hook Fit Report (Milestone 3)

The artist side is real: pitch tracking, a guided range test, saved artist
profiles, and hook-fit scoring that drives the transpose recommendation.
139 checks, 0 failed.

## What became real

- **Pitch tracking** (`Source/analysis/PitchTracker`): YIN with cumulative
  mean normalized difference and parabolic lag interpolation, run on audio
  decimated to ~16 kHz. Unvoiced/breath frames are rejected by the CMNDF
  minimum plus an energy floor, so the trail shows a **gap**, never a false
  note. Tracks 45–72 MIDI within 0.35 semitones in test.
- **Live pitch trail**: the Artist Range graph now draws the real tracked
  history (12 s at 45 Hz) with the live note and cents readout above it.
  Nothing sung means "--", not a placeholder melody.
- **Guided range test** (`VocalEngine` + the panel's instruction band):
  four steps — lowest comfortable, centre, highest comfortable, optional
  falsetto. One button drives it (START → NEXT ×3 → FINISH); each step
  collects only *stable* voiced frames and shows a progress bar, and the
  artist controls pacing. Not enough stable pitch produces an honest
  "couldn't build a profile" message rather than a bogus range.
- **Range profiling** (`VocalRangeProfiler`): percentile statistics, never a
  single extreme note — extended ends are the 5th/95th percentiles,
  comfortable the 30th/70th, strong zone the central mass of the mid phase,
  with the nesting enforced whatever the singer does. A stray squeak does
  not stretch the range (pinned in test).
- **Artist profiles** (`ArtistProfileStore`): small JSON files under
  `~/Library/Application Support/Diamond Loopz/KeyGlo/Profiles`. Ranges and
  settings only — never recorded audio. Names are sanitised so a profile
  cannot escape its folder.
- **Hook fit** (`HookFitScorer`): note-duration histogram scored against the
  profile across all nine transpositions — time in the strong/comfortable/
  extended zones, a penalty for sustained notes outside the profile, and
  distance of the median pitch from the strong-zone centre, plus scale
  compatibility with the detected beat. Ties prefer the smaller shift.
- **The pods came alive**: ARTIST FIT, RANGE FIT and HOOK MATCH now show real
  scores; the wheel's RECOMMENDED and the transpose card's ESTIMATED FIT
  follow the engine. The result card tracks the **selected** shift, with a
  gold "BEST: −2 ST (94)" badge when the user auditions something else.

## Two bugs the work exposed (both fixed, both pinned by tests)

1. **A vocal take could overwrite the beat's key.** Auto re-analysis ran
   every ~3 s on whatever was in the ring, so singing over an analysed beat
   silently replaced "A major from your beat file" with the singer's key.
   Verified by disabling the guard: the test fails with *"still E from LIVE
   INPUT"*. Auto-analysis is now suppressed while a file result stands; the
   refresh button still forces a live pass.
2. **HOOK MATCH sat at a hard 0.** Fit is scored continuously, so a score
   computed before any key existed stored `hookMatch = 0` — which reads as
   "terrible match" rather than "not known yet" — and nothing re-scored it
   when the key arrived. Publishing a beat result now re-scores the fit. The
   same publish also carries `hasProfile`, which had left SAVE PROFILE greyed
   out while a profile was loaded.

## A test-harness lesson worth keeping

Four tests failed the moment the fit path went in, and the cause was the
harness, not the engine: the tests pushed ten seconds of audio into
`processBlock` in a tight loop, while the vocal worker deliberately caps how
much backlog it pitch-tracks per pass (so a stalled worker cannot stampede).
Most of the audio was skipped. Real hosts deliver in real time, so the tests
now do too (`feedPaced`). A harness that delivers audio faster than reality
tests a system that does not exist.

## Verification (139 checks, 0 failed)

Beyond milestone 2's fixtures: pitch tracked at five known MIDI notes;
filtered noise and digital silence produce no voiced frames; profile nesting,
outlier robustness and falsetto handling; hook fit recommending down for a
too-high hook and up for a too-low one, with the recommendation beating the
original; in-scale vs out-of-scale hooks (0.97 vs 0.00 match); a refusal to
score a fragment; profile save/load round-trip and path-escape safety; and
two end-to-end runs through the processor — a sung phrase filling the trail
and scoring, and the beat-protection case above.

## Deferred to Milestone 4

- 808/sample tuning (transient/sustain split, pitch-envelope warning).
- Preview pitch shifter and loudness-matched A/B audio — TRANSPOSE PREVIEW
  currently reports the music theory and the fit honestly, but does not yet
  shift the audio.
- Explicit "capture hook" arming in the UI (the engine supports it; the
  panel currently scores the rolling 12 s window).
