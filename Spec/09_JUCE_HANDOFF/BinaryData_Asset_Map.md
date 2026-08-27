# BinaryData Asset Map

Recommended resource identifiers after JUCE/CMake sanitization:

- `keyglo_shell_1491x1055_png`
- `keyglo_header_logo_250x58_png`
- `key_wheel_base_1024_png`
- `key_wheel_orbit_256px_64frames_vertical_png`
- `score_pod_cyan_256_png`
- `score_pod_violet_256_png`
- `score_pod_gold_256_png`
- `macro_knob_cyan_160px_128frames_vertical_png`
- `macro_knob_gold_160px_128frames_vertical_png`
- `artist_range_grid_690x330_png`
- `vertical_piano_88x440_png`
- `horizontal_note_map_720x110_png`
- `tuner_dial_gold_512_png`
- `stereo_meter_trough_78x124_png`

Add the entire `01_BRAND`, `02_BASE`, `03_HUD`, `04_CONTROLS`, `05_PANELS_CARDS`, `06_VISUALIZERS`, and `07_ICONS` folders to the CMake BinaryData target. Cache the decoded assets once in the editor.
