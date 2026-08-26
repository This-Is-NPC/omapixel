# Animation

Clips are named sequences. Frames are zero-based. Use a stable clip ID where
automation must survive a rename.

## Build a Clip

```bash
omapixel clip sprite.omapixel add walk --fps 8
omapixel frame sprite.omapixel dup --clip walk
omapixel edit sprite.omapixel shift --clip walk --frame 1 --by 0,1
```

For an animated layer, a mutation must name `--frame N`, `--scope frame`, or
`--scope all-frames`. Omapixel refuses to guess when multiple frames exist.

```bash
omapixel paint sprite.omapixel --clip walk --frame 2 \
  --layer-id hero --scope frame --at 8,12 --slot R
omapixel edit sprite.omapixel clear --clip walk \
  --layer-id shadow --scope all-frames
```

Use a batch when creating or editing several frames so a failed command cannot
leave a half-built animation.

## Inspect Frames

```bash
omapixel info sprite.omapixel
omapixel text sprite.omapixel --clip walk --frame 2
omapixel show sprite.omapixel --clip walk --frame 2 --checker
```

Inspect representative and boundary frames, not only frame zero.

## Export

```bash
omapixel render sprite.omapixel -o walk.gif --format gif \
  --clip walk --scale 8 --fps 8 --loop
omapixel render sprite.omapixel -o walk-sheet.png --clip walk \
  --sheet --scale 8 --checker
```

GIF exports the whole selected clip. `--frame`, `--sheet`, `--checker`, and
isolated-layer rendering are PNG operations and are refused for GIF output.
