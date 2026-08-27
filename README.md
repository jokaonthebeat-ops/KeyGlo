# KeyGlo

Artist-to-beat key matching plugin for Diamond Loopz. Detects a beat's key,
scale, BPM and tuning offset, profiles an artist's vocal range, scores how a
hook fits the beat, recommends transpositions, and tunes 808s/samples - all
local, permanently free, no login.

Built from the approved handoff pack in `Spec/` (KeyGlo_UI_Assets_v1.0).
`Spec/09_JUCE_HANDOFF/CLAUDE_MASTER_BUILD_PROMPT.md` is the product spec.

## Status — 0.9.5, feature complete

All four engine milestones plus the product milestone are done; every
control in the interface runs on real analysis or real state.

| Milestone | What it delivered | Report |
|---|---|---|
| 1 — Animated UI | The approved 1491x1055 interface, user-approved | `docs/UI-MILESTONE-REPORT.md` |
| 2 — Beat analysis | Key, scale, BPM, tuning; honest "no reliable key" | `docs/ENGINE-MILESTONE-REPORT.md` |
| 3 — Vocal & fit | Pitch tracking, range test, profiles, hook fit | `docs/VOCAL-MILESTONE-REPORT.md` |
| 4 — 808 & preview | Sample tuning, Apply Tune, preview shifter | `docs/SAMPLE-TUNE-MILESTONE-REPORT.md` |
| Product | Presets, undo/redo, save, MIDI scale export | `docs/PRODUCT-MILESTONE-REPORT.md` |

`make test` = 212 checks, 0 failed. Remaining before 1.0: universal build →
installer → notarisation (needs Apple credentials), and a human listen in a
real DAW.

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

- Footer shows the real version and `LOCAL` instead of the mockup's
  `ONLINE` - the spec promises a fully local product.
- The mockup's stray `MOTE` text in Auto-Tune Setup (a typo artifact) is not
  reproduced; the panel renders KEY / SCALE / MODE.
- **Defaults are Original / 0 cents**, not parameters.json's `-2` / `+4`:
  those are the mockup's demo values, and as defaults they would transpose
  the user's program unasked. The recommendation shows as a gold
  `BEST: -2 ST` badge instead. `make uishot ARGS="out.png def demo"`
  reproduces the mockup's values for reference shots.
- Seven layout/geometry deviations are listed in the UI milestone report.
