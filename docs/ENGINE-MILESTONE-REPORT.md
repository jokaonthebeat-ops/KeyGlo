# KeyGlo — Beat Analysis Engine Report (Milestone 2)

Real key, scale, BPM and tuning detection behind the approved interface,
plus the v2 logo. 92 checks, 0 failed.

## What became real

- **Key & scale** (`Source/analysis/BeatKeyDetector`): 32768-point
  overlapping FFTs (1.5 Hz bins - bass lands on the right semitone), spectral
  peak picking with magnitude compression, tuning-offset estimation *before*
  chroma binning, 12-bin chroma with the bass register (< 250 Hz) weighted
  2.2x, Krumhansl-Schmuckler major/minor correlation over all 24 keys,
  ranked alternatives, and honest gates: percussion/noise reports
  **NO RELIABLE KEY** instead of inventing one.
- **Tempo** (`Source/analysis/TempoDetector`): spectral-flux onset envelope,
  detrended autocorrelation over 55–220 BPM, parabolic lag refinement,
  half/double-time candidates, and a walk-down rule so a groove with an onset
  on every beat reports the beat level, not the half-note.
- **Live capture** (`Source/analysis/CaptureRing`): 12-second lock-free ring
  written pre-trim from processBlock; silence gating is per-channel energy
  (out-of-phase stereo is never mistaken for silence).
- **Coordinator** (`Source/analysis/AnalysisCoordinator`): low-priority
  worker thread. Dropped files decode and analyse asynchronously (WAV/AIFF/
  FLAC/MP3); the session ring re-analyses automatically every ~3 s while
  audio flows, or on the beat panel's refresh button. Host BPM wins in live
  sessions per the spec. A fast lane publishes the real input spectrum +
  chroma at ~20 Hz for the wheel pulse and the analyser display.
- **UI truth**: drag & drop actually analyses; every beat-derived readout
  (rows, confidence bar+pod, wheel key/scale/root, note map, Auto-Tune
  key/chips/keyboard, Transpose NEW KEY) follows the real result. The note
  map is now live-drawn so it can change key - the pack art's highlights are
  baked to the demo scale.

## Honest empty states

A fresh instance shows "--" everywhere and **DROP A BEAT** in the wheel until
something is actually analysed. Artist-side values (ARTIST FIT, RANGE FIT,
HOOK MATCH, ESTIMATED FIT, current note/trail) remain "--" or ambient
placeholder animation until milestones 3/4 deliver their engines - they are
labelled by their panels and never presented as beat-analysis output.
`make uishot ARGS="out.png def demo"` reproduces the approved-reference
state for overlay QA and marketing.

## Verification (92 checks, 0 failed)

Ground-truth fixtures in `tools/DspTest.cpp`:

- F# minor chord loop → F# minor, confidence > 0.5, tuning ≈ 0.
- A major loop → A major.
- **Relative-key tie**: A-major chords over an F#1 drone → F# minor (bass
  evidence breaks the tie both ways).
- +30 cents global detune → measured within ±10, key still found.
- White noise → no reliable key (no surviving spectral peaks at all).
- Clicks at 96/120/148 BPM → within ±2 at a valid metrical level.
- **Produced-groove fixture** (pitch-swept 808 + triads + noise bed at
  148 BPM): pinned after it exposed two real defects - see below.
- Ring end-to-end through processBlock → F# minor published, labelled LIVE
  INPUT; live chroma sees the F# energy.
- File end-to-end (temp WAV) → A major, file name carried, host BPM never
  claimed for files.
- Out-of-phase stereo ring gate; silence claims no tempo.

## Defects the groove fixture caught (and the fixes)

1. **Tonality gate deflated by groove modulation**: peak-share counted only
   local-maximum bins, but an 808 with kick AM/FM spreads into sidebands
   that are not maxima. Fix: count the ±2-bin neighbourhood of each accepted
   peak.
2. **Chroma-spread gate rejected rich harmony**: a loop covering all seven
   scale degrees has near-flat chroma (spread 0.33 vs the 0.55 gate).
   Replaced with the in-scale energy share of the winning key (produced
   groove 0.68, clean loops ~0.9, flat noise can only reach 7/12) - the
   discriminator that survives full-scale harmony.
3. **Tempo picked the half-note**: autocorrelation inherently favours longer
   lags. Fix: walk down to the fastest metrical level within 82 % of the
   best evidence.

## Logo v2

User-supplied chrome/neon wordmark (2049x562 opaque art in a 2172x724
transparent canvas) at `Assets/Brand/keyglo_logo_v2_2172x724.png`; cropped
to opaque bounds at load, aspect-fitted left-anchored in the header, one
high-res decode for every scale. Pack exports remain as fallback.

## Deferred

- Milestone 3: vocal pitch tracking, range profiling, hook fit scoring
  (unlocks ARTIST FIT / RANGE FIT / HOOK MATCH / ESTIMATED FIT and the
  transpose recommendation).
- Milestone 4: 808/sample tuning + preview pitch shifter.
- Cadence/root evidence beyond bass weighting; key detection over
  host-synced bar windows.
