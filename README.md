# KeyGlo

Artist-to-beat key matching plugin for Diamond Loopz. Detects a beat's key,
scale, BPM and tuning offset, profiles an artist's vocal range, scores how a
hook fits the beat, recommends transpositions, and tunes 808s/samples - all
local, permanently free, no login.

Built from the approved handoff pack in `Spec/` (KeyGlo_UI_Assets_v1.0).
`Spec/09_JUCE_HANDOFF/CLAUDE_MASTER_BUILD_PROMPT.md` is the product spec.

## Status

- **Milestone 1 - exact animated UI**: in progress (0.9.0). Full 1491x1055
  interface from the approved mockup, animated from the pack's placeholder
  dataset; honest pass-through audio path with output trim + peak meters.
- Milestone 2 - beat analysis engine (chroma/HPCP key detection, BPM, tuning).
- Milestone 3 - vocal range profiling + hook fit scoring.
- Milestone 4 - 808/sample tuning + preview transposition DSP.

## Building (macOS, this machine)

Hand-rolled Makefile, same toolchain as SourceGlo/MasterGlo (CLT only, no
Xcode). **Cap parallelism at `-j 2`.**

```
make -j 2            # VST3 + AU + Standalone into build/dist
make uishot          # headless editor render -> build/KeyGlo-ui.png
make dsptest         # deterministic test suite
make test            # asset integrity + VST3 probe + dsptest
make install         # copy bundles into ~/Library/Audio/Plug-Ins
make universal       # x86_64 + arm64 (then: make installer)
```

Overlay QA against the approved reference:

```
make uishot ARGS="KeyGlo-ui.png def signal"
python3 tools/overlay.py build/KeyGlo-ui.png
```

## Identity

`com.diamondloopz.keyglo`, plugin code `KGlo`, manufacturer `DmLz`,
AU type aufx. Formats: VST3, AU, Standalone (macOS); VST3/Standalone via CI
for Windows later (same CMake route as MasterGlo - not built yet).

## Pack corrections applied at load (do not "fix" the assets on disk)

- Knob filmstrips are rotated +90 degrees from standard rotary orientation;
  every frame gets a 90-degree CCW pixel transpose at load (Assets.cpp).
- Header logo exports carry transparent margin (155x44 art in a 250x58
  canvas); cropped to opaque bounds at load and aspect-fitted.
- Filmstrips are sliced into frames at load - the 160x20480 strips exceed
  DAW texture limits if drawn directly (MasterGlo lesson).

## Documented deviations from the mockup

- Footer says `v0.9.0` (real version) and `LOCAL` instead of the mockup's
  `ONLINE` - the spec promises a fully local product.
- The mockup's stray `MOTE` text in Auto-Tune Setup (a typo artifact) is not
  reproduced; the panel renders KEY / SCALE / MODE.
