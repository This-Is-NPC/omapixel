# How it is built

This page is for working *on* omapixel rather than with it: the shape of the
project, and the handful of decisions everything else follows from.

## One core, two front ends

```
src/core/    the model, and no opinion about how it is driven
src/cli/     the command line
src/gui/     the studio
```

`core` is a static library. Both front ends link the same object file, and that
is the whole reason this is C++ and not two programs: `resize` used to exist
once in QML and once in Python, with a comment in each pointing at the other.
Now there is one implementation, and the tests cover it once for both.

The core has no idea a window exists. The CLI has no logic of its own: it parses
arguments and calls the core. `DocumentModel` adapts types for QML and holds no
rules. That split is what makes the tests worth having: a rule tested once is a
rule both front ends obey.

```
mise.toml        every task: deps, build, test, check, cli, studio, i18n,
                 format/properties, CLI/Studio E2E, benchmark, config, install
omapixel.pro     subdirs, core first
src/core/
  Grid           one frame: a rectangle of palette slots, one character each
  Palette        slot letter -> colour, in the order the author put them
  Document       size, palette, clips; every mutation, once
  Codec          the format, read and written
  Ops            paint, line, rect, fill, shift, flip, swap, diff
  Render         to PNG, to ANSI, to text
  Bridge         somebody else's sprite catalog, in and out
  Strings        the words, from i18n/<language>.json
  Toml           the subset of TOML a settings file is written in
  Config         the settings and the keymap, from config.toml, followed live
src/gui/
  DocumentModel  the core, made visible to QML; adapts types, owns the history
  InputLog       pointer input, logged either side of the QML boundary
  PixelGridItem  the drawing surface, painted from Render at scale 1
  Theme          the omarchy theme, followed live
  qml/           Main, Surface, Timeline, LayerDock, LayerToolWindow, controls
src/cli/         Commands: what each command does, over an open document
                 main: arguments and files, and the `batch` loop
i18n/            one JSON catalogue per language; en.json is the full list
config/          the annotated default config.toml
examples/        two documents, one of them animated, and the script that built it
tests/           QtTest over the core and QML, plus CLI/format/property/E2E gates
docs/            how to use it and how to run acceptance gates
```

## Everything the studio can do, the command line can do

Not a convenience. It is what lets a script, a Makefile or an agent produce and
inspect art without a display, and it is why there is no operation that exists
only behind a button.

Two commands make the difference between *can change a drawing* and *can work on
a drawing*: `show`, which puts the frame in the terminal, and `render`, which
writes a PNG. Without a way to **see** the result, a complete command set still
leaves you screenshotting a running window.

## It follows the desktop

### The theme

The window reads `$XDG_STATE_HOME/omarchy/current/theme/colors.toml`, the same
file the omarchy shell reads, and follows it live: `omarchy theme set <name>`
with the studio open recolours it along with the bar, without a restart. A
`QFileSystemWatcher` sits on the colours file *and* on the parent of the symlink,
because a theme swap repoints the link rather than changing any file the old
watch was holding.

Only the five roles omarchy publishes are read: background, foreground, accent,
urgent (from the theme's `red`, which is what a theme author tunes for alarm),
muted. Everything else is computed from those, so a theme that defines only the
basics still opens a coherent window.

The surfaces are **mixed toward the foreground** rather than scaled with
`lighter()`, which is what makes them work on `last-horizon`: scaling the value
of `#0c0b0c` by 135% gives `#101010`, so a panel built that way is invisible on
exactly the dark themes omarchy ships most of. Mixing moves a fixed distance
wherever the background starts, and it separates in the right direction under
either mode without asking which mode it is in. Controls are omarchy's
translucent wash over the surface plus a hairline border, not a second opaque
colour, so the same chip reads correctly on any theme.

### Corners

Whether a theme has round or square corners is not in `colors.toml`: it is
Hyprland's `decoration:rounding`, which a theme may set in its `hyprland.lua` and
which the user's own Hyprland config overrides. The studio reads it with
`hyprctl -j getoption` at startup and again when the theme changes, and uses the
number verbatim as the radius of every control, exactly what the omarchy shell
does with `Style.cornerRadius`. So the studio is square on a square desktop and
round on a round one, rather than shipping a radius of its own.

It is asked for twice on a theme change, immediately and again a moment later:
the colours land before Hyprland has applied the new theme's `hyprland.lua`, so
asking once reads the outgoing theme's corners.

With no `hyprctl` the corners stay square, which is what every omarchy theme but
`solitude` asks for anyway. On a machine that is not running omarchy the colours
file is simply absent and the studio opens on its built-in defaults.

### The firewall

The desktop theme never touches the **document's** palette. The art must not
change colour because somebody switched themes, and the window must not change
because somebody recoloured a character. The chequerboard behind transparency is
the one place they meet, and it belongs to the window.

### Settings, in the shape the desktop already uses

`~/.config/omapixel/config.toml` is the arrangement `herdr`, `voxtype` and the
rest of an omarchy machine use: one commented TOML file per program, every
setting present and commented out at its default, a `[keys]` table where an
action is named on the left and the keys that fire it on the right, and
`omapixel --default-config` to print the whole thing. A studio that invented a
fourth arrangement would be the one window on the desktop you have to learn
separately.

With no file at all the program runs on the defaults; the file is how you
disagree, not how you start. It is watched, so saving it rebinds the keys and
redraws the menus without a restart. A keybinding file you have to relaunch to
try is a keybinding file that goes unedited. `omapixel config check` reports a
bad value, a setting nothing reads, a binding that names no key, and two actions
sharing one key: a misspelled setting does nothing and says nothing on its own,
which is the failure a config file actually has.

The full list is in [Settings and keys](configuration.md).

## Somebody else's file

A *catalog* is one JSON document holding many sprite sets, keyed by name and
variant, each with its own named sequences. `import` pulls one set out into a
document; `export` puts the clips back.

A round trip that draws nothing hands the catalog back with the same content, and
a test holds that. It is the property that lets you open somebody else's art
without fear. Key *order* is not preserved, because Qt sorts JSON object keys;
every frame is.

`export` refuses a clip the catalog does not already know rather than creating a
new key: art under an invented name is art nothing ever draws, and the author
only finds out when it fails to appear. It refuses a broken document outright.

`Bridge` is the only class that knows any of this. It exists so a consumer's
vocabulary, its names for sets, variants and sequences, stays in one file
instead of spreading through the model. Another consumer tomorrow gets another
class beside it, and neither the core nor the format changes.

## What the tests hold

`mise run test` runs them all offscreen, so they need no display.

The layer tool is intentionally a second native `QQuickWindow` in the same QML
engine, not a second `DocumentModel`. Its transient parent is the Studio for
window-manager association, while `Qt.Window` keeps it top-level and its
geometry independent. Therefore the CLI/session contract remains one process,
one document, and one process-bound IPC session: opening, moving, focusing, or
closing the tool cannot alter CLI defaults or create another `where` entry.

The native-layer hardening gate extends that test boundary across real
subprocesses: `mise run layers-e2e` launches the offscreen Studio and exercises
CLI/session/render/flatten behavior against the same temporary document. The
canonical benchmark is a separate 60-second stop-rule profile and is included
in `mise run check`.

Beyond the model, three of them exist to stop documentation and code drifting
apart, which is a failure nothing else notices:

- the shipped `config/config.toml` is parsed and compared, line by line, against
  the defaults compiled into `Config`, because a default file that describes a
  program that does not exist is worse than no file;
- every `T.t("…")` in the QML must have an English string, **and** no English
  sentence may be written into the QML instead of asked for by key;
- every `cfg.shortcuts.x` and `cfg.keys.x` the window asks for must be an action
  the keymap knows.

## Looking at the window without a screen

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  OMAPIXEL_SHOT=/tmp/studio.png omapixel-studio drawing.json
```

For layer-window review, the screenshot harness can capture both native windows
and their independent geometry without a desktop compositor:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  OMAPIXEL_SHOT_LAYER=/tmp/layer-tool.png \
  OMAPIXEL_SHOT_LAYER_GEOMETRY=/tmp/layer-tool-geometry.json \
  omapixel-studio drawing.json
```

The combined image places the Studio capture beside the separately captured
top-level tool; the JSON records each native window's coordinates and size.

Renders the window to a PNG and exits. The studio was the one part of this
project that could not be inspected without a display, so a layout change had to
be described and taken on trust. `OMAPIXEL_SHOT_SHEET=colour` opens a panel first,
since a popup does not appear in a window grab, and `OMAPIXEL_DEBUG_INPUT=1` logs
every wheel and gesture event as it arrives. Input that never arrives and input
that arrives and is ignored look identical from the outside.
