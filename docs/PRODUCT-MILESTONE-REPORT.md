# KeyGlo — Presets, Commands & Export Report (0.9.5)

The last stubbed controls became real. Nothing in the interface is a
placeholder now. 212 checks, 0 failed.

## What became real

- **Preset bank** (`Source/presets/PresetManager`): six factory presets
  (the mockup's own names, voiced per workflow) plus user presets as JSON
  under `~/Library/Application Support/Diamond Loopz/KeyGlo/Presets/User`.
  Prev/next step the real bank, the field shows the current name, and a
  **cyan dot** marks a preset whose parameters have drifted.
- **The preset contract**: presets cover only the creative parameters
  (range/key sense, smooth, preview mix, fine tune, transpose, A/B). Output
  trim, bypass and Solo are excluded, so loading a preset never jumps your
  level or your monitoring. Verified by test.
- **Save** writes a user preset (name prompt, sanitised filename), selects
  it, and clears the modified state.
- **Undo/Redo** across parameter moves and preset loads, with the buttons
  enabling and greying correctly.
- **Session restore**: the preset name travels in the host state, so a
  session reopens on the right preset — and a *modified* preset comes back
  modified, not silently reset.
- **COPY SCALE drag-export**: click still copies the text setup; **dragging
  the button into your DAW drops a one-octave MIDI scale file** in the
  recommended key (`KeyGlo F# Minor scale.mid`). Verified by parsing the
  written file back: 8 notes, ascending, one octave, every note in scale.

## Two defaults corrected (documented deviation)

`parameters.json` ships `transposeSemitones = "-2"` and
`fineTuneCents = +4`. Those are the **mockup's demo values**, and as
shipped defaults they would transpose and detune every user's program the
moment a beat is analysed and the preview arms. KeyGlo defaults to
**Original / 0 cents**: the engine's recommendation now appears as a gold
**BEST: −2 ST (99)** badge on the result card — a suggestion the user
clicks, never something applied for them. The demo screenshot sets the
mockup's values explicitly so the approved-reference overlay is unchanged.

## The bug this milestone taught

**APVTS parameter changes do not reach the tree's UndoManager.** Only
direct `ValueTree` edits are recorded; `setValueNotifyingHost` bypasses it
entirely. Passing `&undoManager` to the APVTS constructor looks like it
wires undo up, and it silently does nothing for parameter moves — the first
three undo tests failed for exactly this reason. The fix is an explicit
`ParameterChangeAction` holding before/after value maps, pushed by a
periodic capture from the UI timer (whose first `perform()` is a no-op,
since the change has already happened). That capture is also what makes a
knob **drag** collapse into one undo step instead of hundreds — pinned by
its own test.

## Verification (212 checks, 0 failed)

New: factory bank size and preset-0-equals-defaults; stepping and wrapping;
the excluded-parameter contract; modified tracking both directions; user
preset save/reload round-trip; path-escape safety; session restore of name,
values and modified state; undo/redo of moves and preset loads; drag
coalescing; and the MIDI scale file parsed back note by note.

## Remaining before 1.0

- Universal build → installer → notarisation (the MasterGlo/SourceGlo
  release path is ported and waiting; notarisation needs your Apple
  credentials).
- Help button content, and the settings menu could grow a profile picker.
- **A human listen in a real DAW session** — still the one thing no test
  replaces.
