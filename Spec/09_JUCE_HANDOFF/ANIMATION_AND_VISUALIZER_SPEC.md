# Animation and Visualizer Specification

## Central wheel

- Idle orbit: 4.5 degrees/second.
- Analyzing orbit: ramp to 32 degrees/second over 350 ms.
- Root node: pulse from 1.00× to 1.11× using chroma energy.
- Other scale nodes: 1.00× to 1.07×.
- Non-scale nodes: no pulse; retain faint rim.
- Outer cyan and violet arcs rotate in opposing directions at different speeds to avoid a mechanical loop.
- Score changes tween over roughly 420 ms with ease-out interpolation.

## Artist pitch trail

- Twelve seconds of history.
- Unvoiced frames create a gap rather than a false zero note.
- Color moves from cyan in the comfortable zone to violet in the strong/high area and red only when clearly outside the extended profile.
- The newest point has a small glowing endpoint.

## Spectrum

- 30–45 display updates per second.
- Log-frequency mapping from 20 Hz to 20 kHz.
- Short peak hold, then controlled falloff.
- Cyan at low/mid frequencies, violet toward upper frequencies.

## Tuner

- Needle spring frequency around 8.5 Hz, damping 0.72.
- Gold rim intensifies within ±5 cents.
- Center-note label crossfades when the detected note changes; it must not flicker rapidly between adjacent notes.

## Interaction

- Hover fade: 120 ms.
- Pressed state: 70–100 ms compression plus click ripple.
- Optional pointer parallax: no more than ±2 design pixels.
- Preset changes animate values but never slide the entire layout.

## Performance modes

- Full: 60 fps, orbit, particles, parallax.
- Low power: 30 fps, no particles, half-rate spectrum.
- Reduce motion: stop continuous orbit/particles/parallax; preserve essential pitch, meters, and tuner movement.
