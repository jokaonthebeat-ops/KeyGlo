# Visualizer Rendering Notes

The grids and HUD assets are static foundations. Live content must be drawn in JUCE above them.

- Spectrum: update prepared display points at 30–45 Hz.
- Pitch trail: append smoothed monophonic pitch samples at 30–60 Hz and scroll the history horizontally.
- Key wheel: map a 12-bin chroma vector to note-node brightness and pulse size.
- Orbit sprite: optional 64-frame visual accent; use at 24–60 fps or reproduce the rotation with vector arcs.
- Range bands: draw comfort, strong, extended, and falsetto zones from the selected artist profile.
- Tuner: use a spring-smoothed cents needle and illuminate the rim when the detected pitch is within tolerance.
- All graphics are local/offline. No cloud renderer is required.
