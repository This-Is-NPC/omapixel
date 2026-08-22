# omapixel

A pixel art and animation studio. Draw, animate, and hand the file back to
whoever is going to use the art.

There are two front ends over one core: a command line that can do everything,
and a window for drawing with a mouse. Anything done in one can be done in the
other, because both call the same code.

## Documentation

| | |
|---|---|
| [Getting started](docs/getting-started.md) | install, then draw and animate something, start to finish |
| [The command line](docs/cli.md) | every command, every flag, with examples |
| [The studio](docs/studio.md) | the window: tools, panels, keys |
| [The format](docs/format.md) | what is in the file, and how to write one by hand |

The rest of this page is how the project is built and why it is shaped the way
it is — for working on omapixel rather than with it.

```bash
mise run deps        # what the build needs, and how to install what is missing
mise run build       # the CLI and the studio
mise run test        # the unit tests
mise run cli -- --help
```

Qt 6 and a C++ compiler. `mise.toml` is the only entry point — there is no
Makefile and no scripts under `bin/`, because two ways to build a project is one
way too many; the second is always the one that rots.

## One core, two front ends

```
src/core/    the model: Grid, Palette, Document, Codec, Ops, Render
src/cli/     the command line
src/gui/     the studio
```

`core` is a static library with no opinion about how it is driven. Both front
ends link the same object file, and that is the whole reason this is C++ and not
two programs: `resize` used to exist once in QML and once in Python, with a
comment in each pointing at the other. Now there is one implementation, and the
tests cover it once for both.

## The command line

Every operation the studio can do is here, and that is deliberate: an agent has
to be able to build a whole animation without a window.

```bash
omapixel new heart.json --size 16x16
omapixel rect  heart.json --from 3,5 --to 12,8 --slot R --filled
omapixel line  heart.json --from 3,8 --to 8,12 --slot R
omapixel fill  heart.json --at 8,10 --slot R
omapixel frame heart.json dup
omapixel edit  heart.json shift --by 0,-1 --frame 1
```

| | |
|---|---|
| `new` `info` `check` | make one, describe one, say what is wrong with one |
| `show` `text` `render` | **look at one** — see below |
| `resize` | change the frame, keeping the drawing centred |
| `clip` | `add` `rm` `rename` `fps` |
| `frame` | `add` `dup` `rm` `move` |
| `paint` `line` `rect` `fill` | draw |
| `edit` | `clear` `shift` `flip` `swap` |
| `palette` | `list` `set` `rm` |
| `batch` | many commands over one document, read once and written once |
| `diff` | what differs between two documents |
| `import` `export` | read and write sprite catalogs |

`--clip` and `--frame` default to the first clip and frame 0, so a document with
one clip needs no flags at all.

### Seeing without a window

Two commands are the difference between *can change a drawing* and *can work on
a drawing*:

```bash
omapixel show   heart.json                    # the frame in the terminal, in colour
omapixel render heart.json -o h.png --scale 12 --sheet
```

`show` prints half-block characters in 24-bit colour, two sprite rows per
terminal row — a terminal cell is about twice as tall as it is wide, and one row
per line gives you a drawing stretched to twice its height, which is the wrong
shape to judge. `render` writes a PNG, and `--sheet` lays every frame of the
clip side by side.

Without these, a complete command set still leaves you screenshotting a running
window. With them the window is optional.

## The format

```json
{
  "size":    { "w": 32, "h": 24 },
  "palette": [ { "slot": "I", "colour": "#1A1B26" } ],
  "clips":   [ { "name": "idle", "fps": 8, "frames": [ ["..II..", "SSIISS"] ] } ]
}
```

A pixel is not a colour, it is a **letter** — an index into the palette. That is
the only choice the format makes, and it is the reason everything else is
simple: recolouring a whole character is editing one line of the colour table,
not repainting a thousand frames. Art with the colour baked into each pixel
cannot do that, which is why changing a figure's shirt in PNG means drawing
another figure.

The size belongs to the **document**, not to each clip: a document is a set of
drawings of the same thing, and a clip of another size in the middle of it is
another document. Whoever wants a 16px icon and a 128px backdrop wants two
files, and will be happier with two.

`.` is always transparent and never appears in the palette. A letter the palette
does not define is not an error: it is skipped at paint time and the pixel stays
empty — a hand-written file with a typo in it has to open so it can be fixed.

Palette and clips are **arrays**, and that is the one thing worth explaining. The
obvious shape is an object keyed by slot and by clip name, and the first version
used exactly that. Qt's `QJsonObject` sorts its keys, so round-tripping through
it silently reorders the palette — and the palette's order is the order the
swatch strip draws in, which is content rather than presentation. Arrays keep the
order without a hand-written parser, which is the other way this could have gone
and a far worse one. Documents in the old shape still open.

### Sprite catalogs

A *catalog* is somebody else's file: one JSON document holding many sprite sets,
keyed by name and variant, each with its own named sequences. `import` pulls one
set out into a document; `export` puts the clips back where they came from. See
[the command line](docs/cli.md#interchange).

A round trip without drawing anything hands the catalog back with the same
content, and a test holds that — it is the property that lets you open somebody
else's art without fear. Key *order* is not preserved, because Qt sorts JSON
object keys; every frame is.

`export` refuses a clip the catalog does not already know rather than creating a
new key: art under an invented name is art nothing ever draws, and the author
only finds out when it fails to appear. It refuses a broken document outright.

`Bridge` is the only class that knows any of this. It exists so that a
consumer's vocabulary — its names for sets, variants and sequences — stays in
one file instead of spreading through the model. Another consumer tomorrow gets
another class beside it, and neither the core nor the format changes.

## The studio

```
mise run studio            # empty document
mise run studio person.json
```

Controls down the left, the drawing surface in the middle, the timeline along
the bottom. It drives the same `core` the CLI does -- `DocumentModel` is an
adapter and holds no rules of its own -- so a document edited in the window and
a document edited from the terminal go through the same code.

### It wears the omarchy theme

The window reads `$XDG_STATE_HOME/omarchy/current/theme/colors.toml`, which is
the same file the omarchy shell reads, and follows it live: run `omarchy theme
set <name>` with the studio open and it recolours along with the bar, without a
restart. A `QFileSystemWatcher` sits on the colours file *and* on the parent of
the symlink, because a theme swap repoints the link rather than changing any
file the old watch was holding.

Only the five roles omarchy publishes are read -- background, foreground,
accent, urgent (from the theme's `red`, which is what a theme author tunes for
alarm), muted. Everything else the window needs is computed from those, so a
theme that defines only the basics still opens a coherent window. The surfaces
are mixed toward the foreground rather than scaled with `lighter()`, which is
what makes them work on `last-horizon`: scaling the value of `#0c0b0c` by 135%
gives `#101010`, so a panel built that way is invisible on exactly the dark
themes omarchy ships most of. Mixing moves a fixed distance wherever the
background starts, and it separates in the right direction under either mode
without asking which mode it is in. Controls are
omarchy's translucent wash over the surface plus a hairline border, not a second
opaque colour, so the same chip reads correctly on any theme.

### Corners

Whether an omarchy theme has round or square corners is not in `colors.toml`: it
is Hyprland's `decoration:rounding`, which a theme may set in its `hyprland.lua`
and which the user's own Hyprland config overrides. The studio reads it with
`hyprctl -j getoption` at startup and again when the theme changes, and uses the
number verbatim as the radius of every control -- exactly what the omarchy shell
does with `Style.cornerRadius`. So the studio is square on a square desktop and
round on a round one, rather than shipping a radius of its own.

It is asked for twice on a theme change, immediately and again a moment later:
the colours land before Hyprland has applied the new theme's `hyprland.lua`, so
asking only once reads the outgoing theme's corners.

On a machine with no `hyprctl` the corners stay square, which is what every
omarchy theme but `solitude` asks for anyway.

The desktop theme never touches the *document's* palette. The art must not
change colour because somebody switched themes, and the window must not change
because somebody recoloured a character. The chequerboard behind transparency is
the one place they meet, and it belongs to the window.

On a machine that is not running omarchy the file is simply absent and the
studio opens on its built-in defaults.

## Layout

```
mise.toml        every task: deps, build, test, cli, studio, install
omapixel.pro     subdirs, core first
src/core/
  Grid           one frame: a rectangle of palette slots, one character each
  Palette        slot letter -> colour, in the order the author put them
  Document       size, palette, clips; every mutation, once
  Codec          the format, read and written
  Ops            paint, line, rect, fill, shift, flip, swap, diff
  Render         to PNG, to ANSI, to text
src/gui/
  DocumentModel  the core, made visible to QML; adapts types, owns the history
  PixelGridItem  the drawing surface, painted from Render at scale 1
  Theme          the omarchy theme, followed live
  qml/           Main, Surface, Timeline, and the controls
src/cli/         Commands: what each command does, over an open document
                 main: arguments and files, and the `batch` loop
tests/           QtTest over the core
docs/            how to use it
```

The core has no idea a window exists, and the CLI has no logic of its own — it
parses arguments and calls the core. That split is what makes the tests worth
having: a rule tested once is a rule both front ends obey.

## Requirements

Qt 6 and a C++ compiler. On Arch / Omarchy:

```bash
sudo pacman -S --needed qt6-base qt6-declarative gcc make
```

`mise run deps` checks for them and prints that line if anything is missing.
