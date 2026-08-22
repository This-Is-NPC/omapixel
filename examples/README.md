# Examples

| | |
|---|---|
| `heart.json` | 16×16, two frames: the drawing from [Getting started](../docs/getting-started.md) |
| `last-horizon.json` | 160×90, 33 slots, one frame: a photograph reduced to pixel art |
| `last-horizon-omarchy.json` | that picture dissolving into the retro-82 `omarchy` wallpaper and back, 25 frames at 12 fps |

```bash
mise run studio examples/last-horizon.json
omapixel show examples/last-horizon.json
```

`last-horizon.png` is that document rendered at 9×, and
`last-horizon-omarchy.gif` is the animation, so both can be looked at without
opening anything.

`last-horizon-omarchy.batch` is the script that made the animation, and it
builds the whole thing from nothing: palette, 25 frames and 31069 drawing
commands, in one pass over one file:

```bash
omapixel new anim.json --size 160x90
omapixel batch anim.json --script last-horizon-omarchy.batch   # 0.6s
omapixel diff anim.json last-horizon-omarchy.json              # 0 pixel(s) differ
```

## How the animation was made

The first frame is `last-horizon.json` pixel for pixel, and the last is
`themes/retro-82/backgrounds/omarchy.webp` at 160×90, which is two colours
exactly: `#05182E` and `#FAA968`. Between them, a **dithered diagonal wipe**:
every pixel switches from its photograph colour to its wallpaper colour once, at
a moment decided by where it is plus an ordered-dither offset, so the edge of
the sweep breaks up into the halftone rather than arriving as a hard line.

A dissolve rather than a crossfade, and the reason is the format. Blending two
colours needs a third, so a crossfade of 33 colours into 2 over twelve steps
would invent hundreds of intermediate slots and turn a readable swatch ramp into
a wall. A dissolve invents nothing: the palette is the 33 the photograph already
had plus the wallpaper's 2, reordered darkest to lightest into one ramp, and
every frame is drawn out of colours you can see in the strip.

It is also what makes the script small enough to keep. Each pixel changes at
most once per direction, so **each frame is drawn as a difference from the one
before it**, with `frame dup` and then only the runs that differ. Frame 1 costs 3600
commands; the twenty-four after it cost about 1100 each, where drawing every
frame in full would have cost 3600 apiece.

The animation runs there and back, so it loops without a cut: frame 1 is the
photograph, frames 13 and 14 hold the wallpaper, and frame 25 is one step away
from the photograph again.

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
its colours into the palette. Here that was eighteen colours for the photograph
and fifteen more taken from two 24×16 boxes around the eyes.

**Quantise the subject after downsampling, not before.** What has to survive is
what the small image needs, and that is not the same set of colours the full-size
photograph would give you.

**A two-pixel detail needs its own slot.** Even with a blue ramp, the pupil at
`#1A465B` landed on the same slot as the eyelid shadow above it and vanished.
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
through `batch` produce a byte-identical document, with `diff` reporting zero
pixels differing, in **0.1 seconds**.
