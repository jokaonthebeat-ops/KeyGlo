# CLAUDE MASTER BUILD PROMPT — KEYGLO

You are building **KeyGlo**, a complete free artist-to-beat key matching plugin in JUCE. The attached asset pack is the approved visual and engineering handoff.

## Non-negotiable visual instruction

Reproduce `00_REFERENCE/KeyGlo_Approved_UI_1491x1055.png` as closely as practical. Do not redesign, simplify, rearrange, or replace its premium controls with stock JUCE widgets. The approved mockup is the visual authority for structure, proportions, panel hierarchy, lighting, cyan/violet/gold balance, and animation placement.

The finished editor must feel alive: the key wheel, note nodes, orbit arcs, spectrum, pitch trail, score pods, tuner, meters, buttons, and hover states must move responsively. Do not flatten the approved screenshot and use it as the whole UI.

## Product purpose

KeyGlo is not a demo. It is a permanent free utility that:

- detects a beat's likely key, scale, BPM, and tuning offset;
- analyzes an artist's comfortable, strong, extended, and falsetto ranges;
- evaluates a performed hook against the beat and artist profile;
- recommends a semitone transposition and estimates the improved fit;
- presents Auto-Tune/pitch-correction setup notes;
- detects the stable pitch of 808s, one-shots, samples, and loops;
- runs locally without cloud processing.

## Project assumptions

- JUCE 7 or 8 with CMake.
- macOS: AU, VST3, and Standalone.
- Windows: VST3 and Standalone.
- Base UI: **1491 × 1055**.
- Lock the editor aspect ratio to **1491:1055** and scale uniformly.
- Use `AudioProcessorValueTreeState` for persistent/automatable parameters.
- Use a CMake BinaryData target or JUCE BinaryData for bundled assets.
- Do not bundle font files. Use system font lookup.
- All analysis remains local. No internet login or server is required after installation.

## Required first milestone: exact animated UI

Before building the final analysis engine, deliver a working UI milestone with realistic placeholder/test data:

1. Editor opens at 1491 × 1055.
2. `02_BASE/keyglo_shell_1491x1055.png` is the static chassis.
3. Every major panel and component matches the approved coordinate map.
4. The central key wheel displays all 12 notes and responds to a simulated chroma vector.
5. Score pods tween smoothly.
6. The pitch trail scrolls and the spectrum moves from generated test data.
7. Transpose buttons, A/B, range test, save profile, copy scale, apply tune, solo, preset navigation, and utility icons have normal/hover/pressed/disabled states.
8. Knobs use the supplied 128-frame filmstrips.
9. Tuner needle and stereo meter animate from test data.
10. Capture a screenshot at exactly 1491 × 1055 and overlay it at 50% opacity against the approved reference. Correct visible panel, wheel, button, and knob misalignment before continuing.

## Required component architecture

Create these components or equivalent clearly separated classes:

- `KeyGloAudioProcessorEditor`
- `HeaderComponent`
- `BeatAnalysisPanel`
- `KeyWheelHUDComponent`
- `ScorePodComponent`
- `ArtistRangePanel`
- `PitchTrailComponent`
- `AutoTuneSetupPanel`
- `TransposePreviewPanel`
- `SampleTunePanel`
- `TunerDialComponent`
- `MacroControlStrip`
- `FooterStatusComponent`
- `AnimationClock` or a centralized visual update scheduler
- `KeyGloAssetCache`
- `AnalysisDisplayModel`

Do not put all painting and logic in one editor class.

## Mandatory assets

- Static chassis: `02_BASE/keyglo_shell_1491x1055.png`.
- Premium logo: `01_BRAND/keyglo_header_logo_250x58.png` at x=35, y=17, w=250, h=58.
- Key wheel: `03_HUD/key_wheel_base_1024.png` plus live note nodes and progress arcs.
- Optional orbit sprite: `03_HUD/key_wheel_orbit_256px_64frames_vertical.png`.
- Score pods: cyan, violet, and gold pod assets.
- Knobs: `macro_knob_cyan_160px_128frames_vertical.png`, `macro_knob_gold_160px_128frames_vertical.png`.
- Visualizer grids and tuner dial from `06_VISUALIZERS`.
- Supplied button states, panel/card skins, meter pieces, and SVG icons.
- Exact bounds: `08_LAYOUT/layout_1491x1055.json` and `control_map.csv`.

## Main UI behavior

### Header

- Use the supplied KeyGlo logo image; do not typeset the brand name.
- Previous/next preset arrows and preset name field.
- Save, Settings, Help, Undo, Redo, Power/Bypass.
- Hover glow and click ripple, but no oversized animation.

### Beat Analysis panel

Display Key, Scale, BPM, Tuning, Confidence, and Alternatives. Use the confidence bar asset. The spectrum must animate from real or placeholder data. The drag/drop zone accepts WAV, AIFF, FLAC, and MP3 where platform decoding support exists. The note-map piano highlights the detected scale.

### Central Key Wheel HUD

- Draw all 12 note nodes at the angles in `03_HUD/note_positions.json`.
- Map normalized chroma energy to node glow, pulse scale, and rim brightness.
- Detected root receives the strongest cyan glow.
- Scale notes glow; non-scale notes remain dark but readable.
- Display detected key/scale, Artist Fit, and recommended semitones in the center.
- Animate values with smoothing; do not snap.
- Outer orbit rotates subtly at idle and accelerates during analysis.
- Key Confidence, Range Fit, and Hook Match pods use their assigned accent colors.

### Artist Range panel

- Live current-note and cents readout.
- Vertical piano with the current note highlighted.
- Graph zones: Falsetto, Extended Range, Strong Zone, Comfortable Range, Extended Range.
- Scrolling 12-second pitch trail with cyan-to-violet gradient.
- Start Range Test guides the user through comfortable low, center, high, and optional falsetto notes.
- Save Profile writes a local profile to user application data. Never write files on the audio thread.

### Auto-Tune Setup

Show the recommended key, scale, mode, allowed notes, and highlighted piano notes. `COPY SCALE` copies a concise text description and may also export a one-octave MIDI scale file. Do not claim universal direct control of third-party tuners.

### Transpose Preview

- Buttons from -4 through +4, including Original.
- Selected button uses the bright cyan selected state.
- Display new key and estimated fit.
- A/B switches between original and recommended preview with loudness-matched crossfades.
- Use a high-quality preview pitch shifter or begin with an explicit placeholder DSP stage. Do not block the audio thread.

### 808 / Sample Tune

- Analyze the stable sustain after the transient.
- Display waveform, detected note, recommended semitone change, and fine-tune cents.
- Tuner needle uses a spring-smoothed motion and gold in-tune illumination.
- Apply Tune processes preview audio or reports settings depending on the selected workflow.

### Macro strip

Exact order:

1. Range Sense
2. Key Sense
3. Smooth
4. Preview Mix
5. Fine Tune
6. Output

Use cyan filmstrips for all except Fine Tune, which uses gold. Display labels above and values below. Use a segmented stereo meter at the right.

## Animation and GPU strategy

- Target 60 fps; provide a 30 fps low-power mode.
- Run animations on the message thread or a dedicated visual timing mechanism, never the audio thread.
- Use prepared data arrays and component-local repaints.
- Optional: attach `juce::OpenGLContext` only to a dedicated visualizer canvas for particles and orbit effects. Provide a complete software-rendered fallback.
- Cache SVG drawables, paths, gradients, and images. Do not allocate or parse resources inside `paint()`.
- Support Reduce Motion: stop continuous orbit/particles/parallax but keep essential meters and pitch feedback.

Follow `08_LAYOUT/animation_tokens.json` and `09_JUCE_HANDOFF/ANIMATION_AND_VISUALIZER_SPEC.md`.

## Analysis engine direction

Implement the engine in milestones:

### Milestone 2 — Beat analysis

- STFT and harmonic/percussive-aware preprocessing.
- 12-bin chroma/HPCP accumulation.
- Major/minor template scoring with confidence and alternate candidates.
- Tuning-offset estimation before chroma binning.
- BPM estimate for dropped files; host BPM for live sessions.
- Show uncertainty instead of forcing a false answer.

### Milestone 3 — Vocal range and hook fit

- Monophonic pitch detection using YIN, MPM, or another tested approach.
- Ignore unvoiced/noisy frames and breath-only sections.
- Build comfortable and strong ranges from stable percentile statistics, not one extreme note.
- Analyze the actual hook note distribution.
- Test candidate transpositions and score how much of the hook sits inside the artist's strong/comfortable zones.

### Milestone 4 — 808/sample tuning and preview

- Separate transient from sustain.
- Estimate the median stable fundamental.
- Detect pitch envelopes and warn when the sample does not have one stable note.
- Add safe preview transposition with smoothing/crossfades.

Full engineering guidance is in `DSP_AND_ANALYSIS_ARCHITECTURE.md`.

## Real-time safety

- No allocations, file access, logging, image work, JSON parsing, locks, or model loading in `processBlock()`.
- Audio thread writes analysis samples into lock-free FIFOs and updates atomics.
- Worker threads perform file analysis, profile saving, MIDI/WAV exports, and expensive spectral work.
- UI reads immutable snapshots or atomics.
- Avoid repainting the entire editor when one meter changes.

## Typography

Preferred system font order: Inter Display, Inter, SF Pro Display, Segoe UI, Arial. Do not bundle font files. Use tabular numbers for dynamic scores and cents values.

## Final visual acceptance

The build is not approved until:

- no stock JUCE sliders, combo boxes, or generic flat buttons are visible;
- all seven main panel boundaries align with the approved screenshot;
- the logo is the supplied logo;
- the key wheel is centered and has the same visual scale;
- animations are smooth and do not cause audio dropouts;
- resizing preserves aspect ratio and panel positions;
- Reduce Motion and low-power modes work;
- the plugin is useful without internet, licenses, trial timers, or locked features.

Build the exact animated UI milestone first and stop for screenshot approval before implementing the complete analysis engine.
