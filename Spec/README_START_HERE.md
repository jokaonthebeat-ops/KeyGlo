# KeyGlo UI Assets Pack v1.0

This package converts the approved KeyGlo mockup into a production handoff for a JUCE plugin project. The approved reference is the visual authority. Claude should reproduce the same layout, spacing, panel hierarchy, and moving visual system—not redesign it.

## Coordinate system

- Base size: **1491 × 1055**
- Locked aspect ratio: **1491:1055**
- Default editor size: **1491 × 1055**
- Recommended minimum: **1044 × 739** at 70%
- Scaling: uniform only; do not reflow panels

## Start with these files

1. `00_REFERENCE/KeyGlo_Approved_UI_1491x1055.png`
2. `00_REFERENCE/KeyGlo_Annotated_Layout.png`
3. `02_BASE/keyglo_shell_1491x1055.png`
4. `08_LAYOUT/layout_1491x1055.json`
5. `08_LAYOUT/control_map.csv`
6. `09_JUCE_HANDOFF/CLAUDE_MASTER_BUILD_PROMPT.md`
7. `09_JUCE_HANDOFF/ANIMATION_AND_VISUALIZER_SPEC.md`
8. `09_JUCE_HANDOFF/DSP_AND_ANALYSIS_ARCHITECTURE.md`
9. `09_JUCE_HANDOFF/QA_ACCEPTANCE_CRITERIA.md`

## Rendering strategy

Use a hybrid production approach:

- static chassis, panels, brand art, grids, and filmstrips come from the pack;
- values, note labels, pitch history, spectrum, meters, progress arcs, and results are live JUCE drawing/components;
- use the supplied animation timing and color tokens;
- do not use the approved screenshot as one flattened finished interface;
- do not use stock JUCE controls.

## Folder map

- `00_REFERENCE` — approved UI, annotated map, wireframe
- `01_BRAND` — premium KeyGlo logo and emblem
- `02_BASE` — clean chassis, panel tile, noise, divider
- `03_HUD` — key wheel, score pods, note nodes, orbit sprite
- `04_CONTROLS` — knob filmstrips, button states, toggles, meters
- `05_PANELS_CARDS` — row, drop-zone, result, and readout skins
- `06_VISUALIZERS` — analyzer grids, pianos, pitch trail, tuner, waveform
- `07_ICONS` — original SVG and PNG icons
- `08_LAYOUT` — coordinates, parameters, data contract, colors, animation
- `09_JUCE_HANDOFF` — Claude prompt, implementation, DSP, QA, starter headers
- `10_PREVIEWS` — asset contact sheets

## Important

The reference contains example values. Replace them with real analysis results while preserving the exact visual treatment. KeyGlo is intended to be fully free, local, and useful—not a locked demo.
