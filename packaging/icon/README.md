# The icon

A pixel heart — the drawing omapixel's own getting-started guide teaches you to
make, which is the one picture the project has a claim to.

It was drawn in omapixel. Three files, and the order matters:

| File | What it is |
| --- | --- |
| `omapixel.batch` | The commands that draw it. **This is the source.** |
| `omapixel.json` | The document they produce — a 16x16 sprite. |
| `omapixel.svg` | What the package installs, at the 128 viewBox the desktop wants. |

## Changing it

Edit the `.batch`, then redraw and reconvert:

```bash
omapixel batch omapixel.json --script omapixel.batch
python3 tosvg.py omapixel
```

Starting over from an empty grid is `omapixel new omapixel.json --size 16x16`
first. The round trip is reproducible: the same batch gives back the same SVG,
byte for byte.

## Why an SVG at all, for pixel art

`hicolor/scalable/apps` is where a desktop looks, and a 16x16 PNG would be
resampled into mush at 128. `tosvg.py` emits one `<rect>` per horizontal run of
one colour, so the SVG is the sprite exactly — hard edges at every size, in
under a kilobyte. One rect per *pixel* would be the same picture in three times
the bytes.

The rounded `#101010` badge under the art is what omacalc and omawrite sit on,
so omapixel looks like it belongs beside them in a dock.
