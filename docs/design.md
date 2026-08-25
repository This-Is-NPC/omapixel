# How it is built

This page is for contributors. User-facing commands and workflows belong in the
[CLI](cli.md) and [Studio](studio.md) guides; this page describes the boundaries
that keep both front ends consistent and the gates that protect them.

## One core, two front ends

```text
src/core/    document model, codec, operations, rendering and shared services
src/cli/     command parsing, batch execution and plugin process hosting
src/gui/     the Studio adapter, session publisher and QML interface
```

`core` is a static C++17 library linked by both executables. It has no window or
argument-parser dependency. The CLI resolves arguments and calls core operations;
the Studio's `DocumentModel` adapts the same model for QML and owns UI state such
as selection, history and the active layer. Document rules stay in the core so a
mutation behaves the same from either surface.

The main components are:

```text
mise.toml        build, test, acceptance, packaging and release tasks
omapixel.pro     qmake subdirectories, with core built first
src/core/
  Document       canvas, clips, layers and document mutations
  Codec          strict v2 JSON parsing and writing
  Ops            drawing and whole-document operations
  LayerOperations merge, flatten and storage conversion
  Render         composite and isolated PNG, ANSI and text output
  Differences    complete document comparison
  Bridge         catalog import and export
  Sessions       authenticated Studio discovery
  Output         bounded and atomic output helpers
  PluginManifest manifest parsing and validation
  PluginRegistry deterministic local plugin discovery
  Config/Toml    live settings and keymap
  Strings        JSON translation catalogues
src/cli/
  Commands       document commands
  PluginCommands plugin inspection and one-shot execution
  main           arguments, files and batch dispatch
src/gui/
  DocumentModel  QML adapter, active layer and undo history
  PixelGridItem  the composed drawing surface
  SessionPublisher process-bound Studio state
  Theme          live Omarchy theme integration
  qml/           Studio, layer tool and controls
tests/           QtTest, Python contracts, E2E fixtures and benchmark
docs/            user and contributor documentation plus JSON schemas
```

## Layer model

Layer IDs are immutable automation identities; names are editable presentation
text. `DocumentModel` exposes ordered layer records and one `activeLayerId` to
QML. Paint, shape, fill, clear, transform and paste operations mutate only that
layer. An explicit `frame` or `all-frames` scope selects animated cels; shared
layers always resolve to their single cel. Locked targets reject mutation.

The Studio layer browser and independent Layer tool share one `DocumentModel`.
Opening or moving the tool does not create another document or CLI session.
Structural multi-selection may reorder, hide, lock or delete several layers, but
painting always has one active target. Merge, flatten and storage conversion are
staged before confirmation, so cancellation and failed guards leave the document
unchanged.

Rendering remains one C++ composite surface rather than one QML item per layer.
The renderer visits visible layers bottom-to-top; isolated rendering addresses a
layer explicitly. The complete storage, composition and quantization rules are in
the [format contract](format.md).

## CLI and Studio boundary

Document operations are shared, but the front ends are not expected to expose
identical workflows. The CLI additionally provides batch, import/export, plugin
hosting and headless rendering. Studio session publication is observational:
`omapixel where` reports the current document, layer, frame and selection, while
mutating commands still require their file and targets explicitly.

Studio publishes one authenticated snapshot over a Linux abstract Unix socket.
Discovery verifies the peer UID, PID, executable and process start time. Opening
the Layer tool does not create a second publication.

## Files and plugins

Document and catalog writes use `QSaveFile` without direct-write fallback and
validate destinations before opening and before commit. This protects ordinary
failure, symlink and alias cases. A hostile same-user process can still rename the
containing directory between final validation and commit; closing that residual
would require descriptor-relative no-replace APIs.

Plugins are trusted local executables, not part of the document model. Core owns
manifest validation and deterministic discovery; the CLI owns bounded process
execution and atomic publication of one artifact. The protocol and security
limits are documented in [Plugins](plugins.md).

## Desktop integration

The Studio follows `$XDG_STATE_HOME/omarchy/current/theme/colors.toml` live and
reads Hyprland's current corner rounding. Missing Omarchy or Hyprland state falls
back to built-in colours and square corners. Desktop colours never alter the
document palette.

Configuration uses Qt's platform `AppConfigLocation`, normally
`$XDG_CONFIG_HOME/omapixel/config.toml`, and can be overridden with
`OMAPIXEL_CONFIG_PATH`. Saving the file updates Studio settings and keybindings
without a restart. See [Settings and keys](configuration.md).

## Tests and acceptance

Run the complete gate with:

```bash
mise run check
```

It includes the C++/QML suite, v2 schema and property contracts, CLI and plugin
contracts, offscreen Studio/CLI/session fixtures, i18n audits, keyboard tests and
the canonical layer benchmark. Useful focused tasks include:

```bash
mise run format-v2
mise run plugin-contract
mise run plugin-e2e
mise run cli-layers
mise run layers-e2e
mise run benchmark-layers
```

The layer benchmark uses a 329x480 canvas, 64 frames at 8 fps and five populated
layers with transparency, opacity and normal/multiply/screen composition. Its
stop limits are composite p95 <= 16 ms, save and load p95 <= 3 s, peak RSS <=
384 MiB through edit/undo/redo, and zero missed deadlines during 480 compositions
over 60 seconds. A threshold failure is an architecture failure; do not relax a
limit to make the gate pass.

## Offscreen Studio review

The screenshot harness makes layout changes reviewable without a display:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  OMAPIXEL_SHOT=/tmp/studio.png omapixel-studio drawing.json
```

`OMAPIXEL_SHOT_LAYER` captures the independent Layer tool and
`OMAPIXEL_SHOT_LAYER_GEOMETRY` records both window geometries. The E2E suite uses
the same path to verify keyboard access, session publication and live reload.
