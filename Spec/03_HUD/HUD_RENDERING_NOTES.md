# KeyGlo HUD Rendering Notes

`key_wheel_base_1024.png` contains only static rings, ticks, and glass. Draw all changing note labels, values, fit scores, recommendation text, glow intensity, and progress arcs in code.

`key_wheel_orbit_256px_64frames_vertical.png` is an optional decorative loop. It contains 64 vertical frames at 256×256 each. The base wheel can instead be animated with vector paths for perfect scaling.

Score pods use separate cyan, violet, and gold artwork. Draw the labels and numeric score in JUCE so the results remain live.
