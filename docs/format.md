# The format

A document is one JSON file. It is meant to be read, hand-edited and generated,
so there is nothing binary in it and nothing encoded.

```json
{
  "size":    { "w": 16, "h": 16 },
  "palette": [
    { "slot": "I", "colour": "#1A1B26" },
    { "slot": "R", "colour": "#F7768E" }
  ],
  "clips": [
    {
      "name": "idle",
      "fps": 4,
      "frames": [
        ["................",
         "......RRRR......",
         ".......RR......."]
      ]
    }
  ]
}
```

## A pixel is a letter, not a colour

Each character of a frame row is a **palette slot** — a key into the colour
table above it. `.` is transparent and never appears in the palette.

This is the only real choice the format makes, and everything else follows from
it. Recolouring a character is editing one line of the colour table, not
repainting a thousand frames. Art with the colour baked into each pixel cannot do
that, which is why changing a figure's shirt in a PNG means drawing another
figure.

A letter the palette does not define is **not an error**. It is skipped at paint
time and the pixel stays empty, so a hand-written file with a typo still opens
and can be fixed. `omapixel check` reports it.

## `size`

`w` and `h`, in pixels, both positive. The size belongs to the **document**, not
to each clip: a document is a set of drawings of the same thing, and a clip of
another size in the middle of it is another document. Whoever wants a 16px icon
and a 128px backdrop wants two files, and will be happier with two.

A frame is read at the size of the rows you actually wrote: the longest row wide,
as many rows tall. Short rows within a frame are padded with `.`, so you never
have to count to sixteen on the last one.

If a frame ends up a different size from `size`, that is not a crash — it opens,
and `check` says so:

```
idle[0]: 12x3, expected 8x4
```

Rendering uses the document's size, so the extra is cropped and the missing
reads as empty. To repair the frames themselves, resize the document to the size
it already claims:

```bash
omapixel resize mine.json --size 8x4 --anyway
```

Every frame is rebuilt at that size, centred. `--anyway` is needed only if the
repair would cut through drawn pixels.

## `palette`

An **array**, not an object, and that is the one thing worth explaining. The
obvious shape is an object keyed by slot, and the first version used exactly
that. Qt's `QJsonObject` sorts its keys, so a round trip silently reordered the
palette — and the palette's order is the order the swatch strip draws in, which
is content rather than presentation. An array keeps the order without a
hand-written parser.

Each entry is `{"slot": "<one character>", "colour": "#RRGGBB"}`. Any single
character works as a slot; the standard palette uses capital letters because
they stay legible in a wall of grid rows.

**One character per pixel is the only ceiling on how many colours a document may
have**, and it is a higher one than it sounds: letters and digits first, then the
rest of printable ASCII, then Latin-1 and Latin Extended-A — over four hundred
slots before anything runs out. `.` can never be one, because it is emptiness;
`"` and `\` are excluded on purpose, since a row of pixels full of escapes is a
row nobody can read; and the studio does not hand out digits, because it keeps
`0`–`9` for its own colour keys. A file that already uses a digit as a slot still
opens and draws — the format allows any character, and only the studio's choice
of *new* slots is narrowed.

The seventeen slots a new document starts with, in order — a dark-to-light ramp
of neutrals, then skin, then accents:

```
I C A K D F L   E H N T S   R Y G B P
```

You are not obliged to keep them. Delete what you do not use, add what you do.

## `clips`

Also an array, for the same reason: the order is the order they are listed in.

| | |
|---|---|
| `name` | unique within the document |
| `fps` | how fast this clip plays; per clip, so one file holds a slow blink and a fast run |
| `frames` | an array of frames, each an array of row strings |

A clip always has at least one frame, and a document always has at least one
clip. Both are enforced when the file is read, so a document that lost them by
hand comes back with an `idle` clip rather than refusing to open.

## Writing one by hand

Nothing stops you generating documents from a script — the format exists partly
for that. A minimal valid file:

```json
{
  "size": { "w": 4, "h": 2 },
  "palette": [{ "slot": "R", "colour": "#F7768E" }],
  "clips": [{ "name": "idle", "fps": 8, "frames": [["RRRR", "R..R"]] }]
}
```

Then check it before trusting it:

```bash
omapixel check mine.json && omapixel show mine.json
```

`check` reports a non-positive size, a duplicate slot, a colour that will not
parse, a clip with no frames, a frame that is not the document's size, and any
letter used but not defined.

## Compatibility

Documents written by earlier versions, with `palette` and `clips` as **objects**
keyed by slot and by name, still open. They are rewritten as arrays the first
time they are saved, and the palette order becomes whatever the sort had already
made it — worth checking the swatch strip once after converting an old file.
