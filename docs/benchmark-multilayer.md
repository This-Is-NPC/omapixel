# Multilayer feasibility benchmark

This is a disposable, test-only prototype for the v2 layer contract. It does
not change the production codec, renderer, GUI, or migration inventory.

Run the repeatable benchmark with:

```bash
mise run benchmark-layers
```

## Canonical workload

- 329x480 canvas
- one 64-frame clip at 8 fps
- five animated layers, with all 320 cels present and populated
- transparent pixels in every generated cel
- layer opacities 255, 192, 160, 224, and 128
- normal, multiply, and screen composition modes
- six RGBA palette entries

The prototype stores each cel as one byte per pixel (palette index, with zero
for implicit transparency), and composes layers bottom-to-top using the v2
integer premultiplied source-over rules. The JSON projection uses the v2
`rows` representation and is parsed back into the compact representation.

## Measurements

- Composite p95: 100 warmup compositions, then 200 timed compositions with a
  reused output buffer, cycling frames 0 through 63.
- Save/load p95: one warm save/load followed by three timed JSON projection
  saves and parses. Save includes projection, compact JSON serialization, and
  file write. Load includes file read, JSON parse, and compact projection.
- RSS: after 80 edits, 80 undo operations, and 80 redo operations. History
  snapshots share unchanged cels and copy only the edited cel.
- Playback: 480 compositions paced to 8 fps over a real 60-second monotonic
  run. A frame is missed only when its composition completes after its 125 ms
  deadline.

The executable prints the environment, protocol, raw metrics, thresholds,
representation size, a checksum, and an overall `PASS`/`FAIL`. A failed
threshold is an architecture stop signal: retain the profiles and costs,
record alternatives, and return the layer architecture to contract review
before any production migration.
