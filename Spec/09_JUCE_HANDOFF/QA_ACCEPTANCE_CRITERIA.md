# QA and Acceptance Criteria

## Visual

- Screenshot at 1491×1055 visually overlays the approved reference without major panel drift.
- Correct premium KeyGlo logo is used at the approved header position.
- No default JUCE rotary sliders, text buttons, combo boxes, or alert styles appear.
- All note labels and dynamic values are live text, not baked into the shell.
- Cyan, violet, and gold accents retain the approved hierarchy.
- Wheel, range graph, transpose area, and tuner occupy the approved bounds.
- HiDPI display is sharp; no blurry full-window scaling artifact.

## Interaction

- Every visible button has hover, pressed, disabled, and keyboard-focus feedback.
- Knobs respond to drag, wheel, double-click reset, and automation.
- Drag/drop rejects unsupported files gracefully.
- A/B changes are click-free and loudness matched where practical.
- Reduce Motion and low-power modes work.

## Audio safety

- No allocations, locks, file I/O, image work, JSON, or logging on the audio thread.
- Opening/closing the editor repeatedly does not crash.
- Timers and optional OpenGL are stopped/detached before destruction.
- Analyzer FIFOs survive sample-rate, block-size, and channel-count changes.
- Bypass does not click.

## Functional free-product promise

- No trial timer.
- No locked feature panel.
- No watermark.
- No required internet after download/installation.
- Key, range, fit, tuner, and export/setup assistance remain available permanently.
