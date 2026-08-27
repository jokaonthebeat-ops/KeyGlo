# JUCE Implementation Specification

## Editor scaling

Compute one uniform scale from the 1491×1055 design size. Center the scaled design rectangle and transform child bounds from `KeyGloLayout.h`. Never stretch X and Y independently.

## Asset cache

Decode PNGs and create SVG `Drawable` objects once. Keep a dedicated `KeyGloAssetCache` owned by the editor. Never decode an image or SVG inside `paint()`.

## Filmstrip knobs

The supplied strips contain 128 vertical frames. Use the slider's normalized proportion to select a frame. Each frame's height is the pixel size embedded in the filename.

## Analyzer data flow

- Audio thread: push mono/stereo analysis samples into lock-free FIFOs and update peak/RMS atomics.
- Analysis worker: consume blocks, perform FFT/pitch/chroma analysis, and publish immutable display snapshots.
- UI: repaint components at controlled rates and draw already-prepared vectors.

Suggested refresh rates:

- key wheel and pitch trail: 45–60 Hz
- tuner and meters: 45–60 Hz
- spectrum: 30–45 Hz
- text results: 10–20 Hz or only when changed

## Rendering

1. chassis shell
2. panel wells/static grids
3. key wheel/tuner base art
4. live analyzer fills and arcs
5. interactive controls
6. labels and values
7. hover/focus/ripple effects

Use cached `juce::Path` objects for rings, tick marks, and repeated shapes. Use `juce::DropShadow` sparingly. Glows should be limited to small accent layers, not full-panel blurs.

## Optional OpenGL

A dedicated visualizer canvas may use `juce::OpenGLContext` for particles and orbit effects. The rest of the UI should remain standard JUCE components. The plugin must work with OpenGL disabled and must safely detach the context before editor destruction.

## Files and profiles

Use `FileChooser` and worker jobs. Artist profiles should be small JSON files stored under the platform's user application-data folder. Store only analysis settings and note ranges—not recorded audio unless the user explicitly exports it.

## Preview DSP

Crossfade original and transposed preview signals with smoothed gains. Keep preview latency reported correctly if the algorithm introduces lookahead. Never process or rewrite the user's source file destructively.
