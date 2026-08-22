# Examples

| | |
|---|---|
| `heart.json` | 16×16, two frames — the drawing from [Getting started](../docs/getting-started.md) |
| `last-horizon.json` | 160×90, 33 slots, one frame — a photograph reduced to pixel art |

```bash
mise run studio examples/last-horizon.json
omapixel show examples/last-horizon.json
```

`last-horizon.png` is that document rendered at 9×, so the file can be looked at
without opening anything.

## How the big one was made

It came from a 3160×1778 photograph, and every step after the first was
omapixel's own command line:

```bash
# outside omapixel: reduce, then choose a palette (see below)
magick photo.jpg -resize 160x90! raw.png
magick raw.png -dither None -remap palette.png flat.png

# from here on, the CLI: one script, one pass
omapixel new wall.json --size 160x90
omapixel batch wall.json --script wall.batch
```

where `wall.batch` is 3672 lines of

```
palette rm --slot I
palette set --slot A --colour "#09090A"
edit clear --slot A                  # the background, in one call
line --from 12,7 --to 19,7 --slot J
paint --at 44,31 --slot T
...
```

Four things are worth knowing before repeating it.

**A global quantiser will destroy the subject.** The first attempt asked for
sixteen colours over the whole photograph. The background alone is a third of
the pixels, so the two blue-greens of the irises got 48 pixels between them and
both eyes came out as white smudges. Quantise the *subject* separately and merge
its colours into the palette — here, eighteen colours for the photograph and
fifteen more taken from two 24×16 boxes around the eyes.

**Quantise the subject after downsampling, not before.** What has to survive is
what the small image needs, and that is not the same set of colours the full-size
photograph would give you.

**A two-pixel detail needs its own slot.** Even with a blue ramp, the pupil —
`#1A465B` — landed on the same slot as the eyelid shadow above it and vanished.
Adding one dark navy entry brought the eye back. When something small matters,
check what it maps to rather than trusting the colour count.

**Order the palette by luminance, and run-length encode the rows.** The slots
come out darkest to lightest, which makes the swatch strip a ramp instead of a
hunt. One `line` per run of the same slot, and none at all for the dominant
colour that `edit clear` already laid down: 3621 calls instead of 14400.

**Use `batch` for anything generated.** The first version of this ran the 3621
drawing commands as 3621 separate invocations and took four and a half minutes,
almost none of it drawing: every call started a Qt application, parsed the whole
document, changed a few pixels and wrote the file back. The same commands
through `batch` produce a byte-identical document — `diff` reports zero pixels
differing — in **0.1 seconds**.
