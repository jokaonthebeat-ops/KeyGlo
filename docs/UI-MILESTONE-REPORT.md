# KeyGlo — UI Milestone Report (0.9.0)

Milestone 1 of CLAUDE_MASTER_BUILD_PROMPT.md: the exact animated interface,
running from the pack's placeholder dataset
(08_LAYOUT/analysis_data_contract.json — the same numbers the approved mockup
displays), over an honest pass-through audio path.

## What is live

- **Key wheel HUD**: 12 note nodes at note_positions.json's chromatic angles,
  chroma-driven pulse (root to 1.11x, scale notes to 1.07x), three
  counter-rotating accent arcs with glowing tips (idle 4.5°/s, analysing ramps
  to 32°/s over 350 ms), live centre readout.
- **Score pods**: cyan/violet/gold pod art, live progress ring, 420 ms
  ease-out score tweens.
- **Beat analysis**: six data rows with hover states, animated log-spectrum
  (40 Hz updates, peak-hold, cyan→violet), drag & drop zone with real
  enter/exit/drop states (accepts WAV/AIFF/FLAC/MP3), note map.
- **Artist range**: live current-note card, vertical piano with strong-zone
  and live-note highlights, five labelled range zones, scrolling 12-second
  pitch trail at 45 Hz with unvoiced gaps and a glowing endpoint.
- **Transpose preview**: −4…+4 buttons driving the real `transposeSemitones`
  parameter (selected state + connector stem), result card on the shell's
  well, A/B bound to `previewRecommended`.
- **Auto-Tune setup**: key/scale/mode, allowed-note chips, live two-octave
  keyboard highlight; COPY SCALE writes a real setup string to the clipboard.
- **808/Sample tune**: gold waveform, spring-damped tuner needle (8.5 Hz,
  damping 0.72, gold rim inside ±5 cents), readout cells, Apply Tune / Solo
  (Solo bound to its parameter).
- **Macro strip**: six 128-frame filmstrip knobs (gold Fine Tune) attached to
  APVTS parameters, double-click returns to parameter defaults, live stereo
  segment meter fed by the real audio path.
- **Header/footer**: preset browser (display-only list until the preset
  milestone), settings menu with working Reduce Motion and Low Power (30 fps)
  modes, power button bound to bypass, honest footer status.
- **Audio**: stereo/mono pass-through with smoothed output trim; bypass glides
  (click-free, verified); peak meters. No allocations/locks/IO in
  processBlock.

## Verification

- `make dsptest`: **57 checks, 0 failed** — parameter roster/defaults vs
  parameters.json, gain path ground truth (−12 dBFS in → −14 out at default
  trim), bypass unity + smoothness, layouts, state round-trip, display-model
  publish, demo-feed determinism, headless editor with all required
  components present and **zero asset load failures**.
- `make uishot ARGS="KeyGlo-ui.png def signal"` + `tools/overlay.py`: 50 %
  blend and difference heatmap against the approved reference. All seven
  panel wells, the knob row, meters, note map and buttons register on the
  shell; remaining differences are animation phase and the items below.

## Pack corrections applied at load

1. **Knob filmstrips rotated +90°** from standard rotary orientation (frame
   64 pointed east, not north) — per-frame 90° CCW pixel transpose at load.
   Same defect as the SourceGlo pack.
2. **Filmstrips sliced into frames** at load (160×20480 exceeds DAW texture
   limits if drawn whole — the MasterGlo "works standalone, missing in DAW"
   trap).
3. **Header logo cropped to opaque bounds** (155×44 of art in the 250×58
   canvas) and aspect-fitted, left-anchored.
4. **Icon SVGs**: the export stamps the stroke colour as the group *fill*,
   which turns e.g. the help "?" ring into a solid disc (the pack's own PNG
   exports show the same bug). Corrected at load: group fills→none, strokes
   take the tint. The icons are stroke-drawn originals.

## Documented deviations from the mockup

| # | Item | Why |
|---|------|-----|
| 1 | Footer reads `v0.9.0` and `LOCAL` instead of `v1.0.0` / `ONLINE` | Honest strings; the spec promises a fully local product |
| 2 | Auto-Tune panel's stray `MOTE` text not reproduced | Mockup typo artifact |
| 3 | Hero cluster (wheel + pods + centre text) is geometrically centred; the mockup's sits ~25 px right of its own panel centre | Rendered-mockup artifact; nodes wobble up to 25° off their own note_positions.json angles, so the JSON's exact 30° steps are used with the node radius (0.39) measured off the mockup |
| 4 | Transpose result card sits exactly on the shell's well {399,680,351,113} | JSON (397,678) and mockup card (414,689) both disagree with the shell they ship with |
| 5 | Auto-Tune chips/keyboard/copy stack kept inside the panel | The mockup's copy button overflows its own panel well by ~7 px |
| 6 | Header wordmark is narrower than the mockup's | The supplied export has tighter letter-spacing (155×44 art); height matched, aspect preserved per LOGO_USAGE_GUIDE |
| 7 | Layout coordinates: knob row, preset cluster, transpose row, A/B, sample readouts, tuner measured off the mockup where layout_1491x1055.json drifts 10–31 px | Mockup is the declared visual authority; JSON values kept as comments in Theme.h |

## Deferred to later milestones (per the build order)

- Real beat/vocal/808 analysis engines (milestones 2–4) — the DemoFeed
  publishes through the same AnalysisDisplayModel the engine will use.
- Preview pitch-shift DSP and loudness-matched A/B audio.
- Preset persistence, Save/Undo/Redo commands, range-test flow, profile
  saving, MIDI scale export.
- Note map switches from the pre-highlighted pack art to a live-drawn piano
  when detection lands (milestone 2).
