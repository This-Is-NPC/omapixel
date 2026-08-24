# The command line

Everything the studio can do, the command line can do. That is deliberate: it is
what lets a script, a Makefile, or an agent produce and inspect art without a
window, and it is why there is no operation that exists only behind a button.

```
omapixel <command> <file> [sub-command] [arguments] [--flags]
```

The file comes **second**, right after the command, and before any sub-command:

```bash
omapixel palette heart.json list      # not: omapixel palette list heart.json
omapixel clip    heart.json add walk
```

Two commands break that pattern because they do not read a document:
`omapixel new <file>` writes one, and `omapixel import <catalog>` reads somebody
else's file.

Changes are written back to the same file. There is no `--save` and no
in-place flag; a command that changes a document saves it.

## Exit codes

| | |
|---|---|
| `0` | it worked |
| `1` | it ran and the answer was no: the file would not load, a resize or trim would crop, `check` found problems, `diff` found differences |
| `2` | the command was wrong: a missing flag, an unknown sub-command |

The split matters for scripting: `2` means fix your command, `1` means fix your
art. `check` and `diff` deliberately return `1` on a finding so they work in a
shell `if` or a CI step.

## Picking a clip and a frame

Almost every command takes `--clip <name>` and `--frame <index>`. Both default to
the first clip and frame `0`, so a document with one clip and one frame needs
neither.

```bash
omapixel show heart.json --clip walk --frame 3
```

Coordinates are `X,Y` with no space, `0,0` at the top left. Sizes are
`COLUMNSxROWS`, as in `--size 32x24`.

---

## Making and looking

### `new` — create an empty document

```bash
omapixel new heart.json --size 16x16
```

`--size` defaults to `32x24`. You get one clip called `idle`, one blank frame,
and the seventeen-colour standard palette.

### `info` — what is in a document, as JSON

```bash
omapixel info heart.json
```

Size, the whole palette, and for every clip its name, fps, frame count and
`drawn`, the number of non-empty pixels summed over its frames. Machine
readable on purpose; pipe it into `jq`.

### `check` — what stops a document from being drawn

```bash
omapixel check heart.json
```

Prints one line per model problem and exits `1` if anything is found. Structural
format errors are refused earlier, while loading, with their JSON path.

### `show` — draw a frame in the terminal

```bash
omapixel show heart.json
omapixel show heart.json --clip walk --frame 2 --checker
```

Half-block characters and 24-bit colour: two sprite rows per text row, at real
proportions. `--checker` puts a checkerboard behind transparency instead of
leaving it as the terminal background.

### `text` — the frame as letters

```bash
omapixel text heart.json
```

The raw grid, one character per pixel, `.` for empty. This is the frame exactly
as it is stored, which makes it the right thing to `diff`, to grep, or to paste
into a bug report.

### `render` — write a PNG

```bash
omapixel render heart.json -o heart.png --scale 8
omapixel render heart.json -o sheet.png --scale 8 --sheet --checker
```

| | |
|---|---|
| `-o`, `--out` | where to write, required |
| `--scale <n>` | screen pixels per sprite pixel, default `1` |
| `--sheet` | every frame of the clip side by side, instead of one frame |
| `--checker` | checkerboard behind transparency instead of alpha |
| `--isolated --layer-id <id>` | render exactly one layer, instead of the composite |
| `--isolated --layer "Exact Name"` | exact name form, including spaces |

Scaling is nearest-neighbour, so a pixel stays a square. It works with no
display attached.

`show` and `text` use the same composite default and isolated target options.
The CLI never reads a selected layer or frame from a Studio session.

### `diff` — what differs between two documents

```bash
omapixel diff mine.json theirs.json
```

Compares the complete document: dimensions, palette values and order, clip names
and order, FPS, frame counts, frame dimensions and pixels. Exit `1` if anything
differs.

### `where` — which live studios hold a document

```bash
omapixel where                # every live studio, as JSON
omapixel where heart.json     # only studios holding that document
```

The studio publishes what it has open while it runs, and this is the read side:
one JSON object per window, with the process id, the document's absolute path,
whether that window holds unsaved work, its current view, and its selected
rectangle or `null`. The view always carries the clip, frame, stable active layer
ID and name, and edit scope (`frame` or `all-frames`). A selection repeats its
clip, frame, layer ID and layer name alongside x, y, width, height, and pixel
count, so an agent can address exactly what the user selected. Ask before you
write: a write to a document a window holds will appear in that window, and
unsaved strokes there move one Ctrl+Z away.

```json
{"sessions":[{"pid":1234,"started":987654,"path":"/work/heart.json","dirty":false,"view":{"clip":"idle","frame":0,"layerId":"hero","layerName":"Hero","scope":"frame"},"selection":{"clip":"idle","frame":0,"layerId":"hero","layerName":"Hero","x":2,"y":3,"width":6,"height":7,"count":42}}]}
```

Without a selected range, `selection` is `null`; `view` is still present.

The result is a point-in-time state snapshot. Playback remains a Studio-local
control. The endpoint is authenticated with Linux `SO_PEERCRED` plus `/proc` PID,
start-time, UID, and executable checks; `started` is `/proc` clock ticks since
boot, not epoch seconds. Discovery and responses are bounded. Legacy sidecars are
stale garbage only and are never parsed as authority.
`where` never supplies defaults to another command: pass the clip, frame, layer,
or scope explicitly to the command you run.

The acceptance fixture creates both storage modes through the CLI, edits by
stable layer ID, and proves that the same document can be inspected by `where`,
rendered as a composite or isolated layer, and flattened to a separate output.
Run it with `mise run layers-e2e`.

An untitled window publishes a scratch path under the runtime directory
(`studio.scratch`), so "draw on what I have open" works before any save — the
file is tmpfs and dies with the session, which is why that window still reads
as unsaved.

Exit `0` when something was printed; exit `1` when the honest answer is nobody
— both for a named document no window holds and for no live sessions at all.
Sessions whose process has died or whose id was recycled are deleted on the way
rather than reported, so the directory cleans itself.

---

## Drawing

All four take `--slot <letter>`, which colour to draw with. Leave it out and
they draw `.`, which erases.

### `paint` — one pixel

```bash
omapixel paint heart.json --at 7,13 --slot R
```

### `line` — from one point to another

```bash
omapixel line heart.json --from 5,11 --to 10,11 --slot R
```

### `rect` — an outline or a filled block

```bash
omapixel rect heart.json --from 2,4 --to 6,8 --slot R           # outline
omapixel rect heart.json --from 2,4 --to 6,8 --slot R --filled  # solid
```

### `fill` — flood the contiguous run under a point

```bash
omapixel fill heart.json --at 7,7 --slot B
```

Spreads through the pixels of the same slot as the one under `--at`, up, down,
left and right. Diagonals do not connect.

### `edit` — whole-frame operations

```bash
omapixel edit heart.json clear                    # every pixel empty
omapixel edit heart.json clear --slot I           # every pixel slot I
omapixel edit heart.json shift --by 0,1           # move the drawing
omapixel edit heart.json flip  --axis x           # mirror (x or y)
omapixel edit heart.json swap  --slot R --to B    # every R becomes a B
```

`shift` moves what is drawn and drops whatever leaves the frame. `swap` is the
one to reach for when a colour is wrong everywhere.

Content mutations on an animated layer can state their scope explicitly:

```bash
omapixel paint heart.json --layer-id hero --scope frame --at 2,2 --slot R
omapixel edit heart.json clear --layer "Hero Layer" --scope all-frames
```

`--scope frame` addresses the selected frame (default `0`); `--scope all-frames`
addresses every frame in that clip. `--all-frames` is an equivalent spelling for
batch scripts. A multi-frame animated edit without a frame or scope is refused
instead of guessing.

---

## Structure

### `clip` — add, rm, rename, fps

```bash
omapixel clip heart.json add walk --fps 12
omapixel clip heart.json rename walk run
omapixel clip heart.json fps --name run --fps 6
omapixel clip heart.json rm  run
```

`add` defaults to 8 fps. A document keeps at least one clip, so removing the last
one is refused: a document with no clips has nothing to draw and cannot be
reopened for editing.

### `frame` — add, dup, rm, move

```bash
omapixel frame heart.json add              # a blank frame after the current one
omapixel frame heart.json dup              # a copy of the current one
omapixel frame heart.json rm   --frame 2
omapixel frame heart.json move --frame 3 --index 0
```

`--frame` says which frame you are acting on; for `move`, `--index` says where it
lands. A clip keeps its last frame.

### `palette` — list, set, rm

```bash
omapixel palette heart.json list
omapixel palette heart.json set --slot R --colour "#FF5577"
omapixel palette heart.json rm  --slot P
```

`set` also adds a slot that was not there. Removing a slot that pixels still use
is allowed: those pixels keep their letter in the file and read as empty until
the slot comes back, which is what makes a wrong `rm` recoverable. `check`
reports it as *uses a slot with no colour: B*, so it does not go unnoticed.

Palette slots are limited to 256 and must be one printable character other than
`.`, `"`, or `\\`; C0/C1 controls and DEL are refused consistently by the CLI,
Studio core, Bridge, and v2 codec.

### `resize` — change the frame, keeping the drawing centred

```bash
omapixel resize heart.json --size 32x32
omapixel resize heart.json --size 8x8 --anyway
```

Every frame of every clip changes together, because the size belongs to the
document.
Growing pads evenly; shrinking crops evenly. If it would cut through drawn
pixels it refuses and tells you how many, and `--anyway` does it regardless.

Resizing to the size the document *already* claims is not a no-op: it rebuilds
every frame at that size, which is how a hand-written file with an off-size
frame gets repaired.

### `trim` — remove empty borders around one frame

```bash
omapixel trim heart.json
omapixel trim heart.json --clip walk --frame 3
omapixel trim heart.json --clip walk --frame 3 --anyway
```

The named frame defines the smallest rectangle containing all its drawn pixels.
Only empty borders outside that rectangle are removed; empty rows or columns
between drawn pixels stay. The same rectangle is applied to every frame of every
clip, preserving their alignment and the document's one shared size.

If another frame has pixels outside that rectangle, `trim` refuses and reports
how many would be lost. `--anyway` confirms the crop. An empty reference frame
is also refused, while a frame that already touches all four canvas edges is a
successful no-op and does not rewrite the file.

### `layer` — inspect and control the layer stack

Layer order is bottom-to-top and indices are zero-based. IDs are stable across
rename and reorder; use them for automation. `--layer` is an exact, unique name,
not an ID-or-name fallback.

```bash
omapixel layer art.json list
omapixel layer art.json add --id hero --name "Hero Layer" --storage animated
omapixel layer art.json rename --layer-id hero --name "Main Hero"
omapixel layer art.json move --layer "Main Hero" --index 1
omapixel layer art.json set --layer-id hero --visible false --opacity 160
omapixel layer art.json mode --layer-id hero --mode multiply
omapixel layer art.json dup --layer-id hero --id hero-copy --name "Hero Copy"
omapixel layer art.json rm --layer-id hero-copy
omapixel layer art.json merge-down --layer-id hero
```

`set` accepts `--visible true|false`, `--locked true|false`, and
`--opacity 0..255`. `add` and `dup` require the new `--id` and `--name` so the
result is deterministic. `merge-down` composites the selected layer into the
layer immediately below it and reports its frame and pixel consequences.

Every mutation against a multilayer document requires either `--layer-id` or an
exact `--layer` name. Supplying both is allowed only when they identify the same
layer. Missing, unknown, negative, or conflicting targets fail with stable
`E_LAYER_*` diagnostics and do not write the document. Renaming or reordering
never changes an ID.

Locked targets refuse content, metadata, reorder, duplicate, remove, and merge
operations. Hidden targets refuse content and destructive stack operations unless
`--include-hidden` is present. Rendering and flattening always omit hidden layers.

### `flatten` — write a separate composite document

```bash
omapixel flatten art.json -o art-flat.json
omapixel flatten art.json -o art-flat.json --anyway
```

`-o` is required and must differ from the input path. The source is never changed.
The command prints a deterministic report containing frames, affected pixels,
exact and approximated palette pixels, new slots, and removed layers. If palette
quantization would approximate any composed pixels, it exits `1` with the report
and requires `--anyway` before writing. `--anyway` confirms only that palette loss;
flattening still refuses if it would remove or change any locked layer.

---

## Many commands at once

### `batch` — one pass over one document

```bash
omapixel batch heart.json --script draw.txt
omapixel batch heart.json --script -          # read stdin
```

Each line of the script is a command with its flags, **without the file**. The
file is the one `batch` opened:

```
# blank lines and # comments are skipped
palette set --slot R --colour "#F7768E"
rect --from 2,4 --to 6,8 --slot R --filled
line --from 5,11 --to 10,11 --slot R
clip add walk --fps 12
frame dup --clip walk
```

Only commands that work on the open document are allowed: `info`, `check`,
`show`, `text`, `resize`, `trim`, `clip`, `frame`, `palette`, `paint`, `line`, `rect`,
`fill`, `edit`, `layer`. Anything that names a second file (`render`, `flatten`, `diff`, `export`,
`import`, `new`) stays outside, so a script cannot quietly write over something
the person running it never mentioned.

**A batch is all or nothing.** If a line fails, it says which line and why, and
nothing is saved. A script that half-applied would leave a document nobody could
reason about, and running it again would not mean the same as running it once.

**Use it for anything generated.** Every separate invocation starts a Qt
application, parses the whole document, changes a few pixels and writes the file
back. Converting a photograph to a 160×90 picture one command at a time took
3621 processes and **4 minutes 26 seconds**; the identical result through
`batch`, on the same document with `diff` reporting zero pixels different, takes
**0.1 seconds**.
The drawing was never the slow part.

---

## Interchange

A *catalog* is a JSON file that holds many sprite sets at once, keyed by a name
and a variant, each with its own named sequences. These two commands move art
between such a file and an omapixel document.

### `import` — pull one set out of a catalog

```bash
omapixel import catalog.json --name person --index 1 -o person.json
```

| | |
|---|---|
| `--name` | which set in the catalog |
| `--index` | which variant, default `0` |
| `-o` | the document to write, required |

Each sequence becomes a clip.

### `export` — put the clips back

```bash
omapixel export person.json catalog.json --name person --index 1
```

`--name` is required. The species and variant must already exist and have the
expected JSON shape; validation happens before any write. The catalog is edited
in place only after that validation. A clip the catalog does not already know is
skipped with a warning rather than added: art under an invented name is art
nothing ever draws. A document with problems is refused outright, so run `check`
first. If every clip is skipped, export fails and leaves the original untouched.

A round trip that draws nothing gives the catalog back with the same content.
Key order is not preserved; every frame is.

---

## Settings

### `config` — the settings file

```bash
omapixel config           # where it is, and what in it differs from the defaults
omapixel config check     # what is wrong with it, exit 1 if anything
omapixel config write     # put the annotated default in place to start from
omapixel --default-config # print that default to standard output
```

The file is `~/.config/omapixel/config.toml`, or `$OMAPIXEL_CONFIG_PATH` when
that is set. Both the studio and the command line read it: the command line for
the language, the studio for everything including every keybinding. With no file
at all, both run on the defaults.

`check` exits non-zero when it finds a bad value, a setting nothing reads, a
binding that names no key, or two actions sharing one key. See
[Settings and keys](configuration.md).

---

## Recipes

Fifty frames of the same clip, as separate files:

```bash
for i in $(seq 0 49); do
  omapixel render walk.json --frame "$i" --scale 4 -o "frame-$i.png"
done
```

A palette swap into a new file, without touching the original:

```bash
cp person.json person-red.json
omapixel palette person-red.json set --slot B --colour "#C04040"
```

Fail a build when the art is broken:

```bash
omapixel check person.json || exit 1
```

Watch what an edit actually changed:

```bash
cp person.json before.json
omapixel edit person.json shift --by 1,0
omapixel diff before.json person.json
```
