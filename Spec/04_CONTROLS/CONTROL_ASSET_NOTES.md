# Control Asset Notes

Button skins contain no text. Render labels and icons in JUCE so the controls remain localizable, crisp, and data-driven.

Knob strips are vertical 128-frame filmstrips. Frame height equals the filename's pixel size. Use `valueToProportionOfLength()` and map to frames 0–127.

The cyan macro knob is used for Range Sense, Key Sense, Smooth, Preview Mix, and Output. Use the gold variant for Fine Tune. The small knob can be used for future compact controls.
