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
| `1` | it ran and the answer was no: the file would not load, the resize would crop, `check` found problems, `diff` found differences |
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

Scaling is nearest-neighbour, so a pixel stays a square. It works with no
display attached.

### `diff` — what differs between two documents

```bash
omapixel diff mine.json theirs.json
```

Compares the complete document: dimensions, palette values and order, clip names
and order, FPS, frame counts, frame dimensions and pixels. Exit `1` if anything
differs.

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
`show`, `text`, `resize`, `clip`, `frame`, `palette`, `paint`, `line`, `rect`,
`fill`, `edit`. Anything that names a second file (`render`, `diff`, `export`,
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
