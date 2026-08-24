# Native layer acceptance

The native layer release gate is intentionally cross-surface. Run it from a
checkout with Qt 6, a C++17 compiler, `make`, and `mise`:

```bash
mise run check
mise run i18n
mise run benchmark-layers
```

`mise run check` includes the C++/QML suite, executable format checks,
deterministic schema properties, the CLI contract, the live Studio fixture, the
i18n catalogue audit, and the canonical benchmark. The standalone commands are
useful when iterating on one surface.

## Canonical environment

The benchmark uses a 329x480 canvas, 64 frames at 8 fps, five populated layers,
transparent cels, non-100 opacity, and normal/multiply/screen modes. It reports
the Qt version, architecture, CPU, protocol, checksum, and every threshold:

- current-frame composite p95 <= 16 ms;
- save p95 <= 3 s;
- load p95 <= 3 s;
- edit/undo/redo peak RSS <= 384 MiB;
- 480 playback compositions over 60 seconds at 8 fps with zero missed deadlines.

A failed threshold is a stop signal. Keep the complete profile and representation
costs, record viable alternatives, and return the architecture to contract review.
Never relax a threshold to make the gate green.

## Fixture coverage

`tests/test_layers_e2e.py` creates animated and shared layers through the CLI,
edits by stable ID, launches the offscreen Studio, checks `where`, verifies a
Studio screenshot, exercises external live reload, renders composite and
isolated PNGs, and confirms flatten refusal plus explicit `--anyway` output.
The C++ fixture `nativeLayersCrossSurfaceAcceptanceFixture` additionally covers
Studio-model paint, undo, save/reload, session publication, and the same render
and flatten contract against real subprocesses.

`tests/test_format_v2_properties.py` uses a fixed seed and generated cases for
canvas dimensions, layer counts, opacity, IDs/names, and shared/animated cel
cardinality. Its mutations must fail without being normalized into a valid file.

The GUI acceptance assertions cover the approved window architecture: the main
window has a simple layer browser and exactly one independent non-modal top-level
Layer tool; row Up/Down plus Enter opens the selected layer, Tab reaches tool
actions, Space activates an action, and Escape closes the tool and returns focus.
The tool's geometry is independently movable/resizable and is bounded by its
minimum size. A consequence sheet returns focus to the still-open tool, while
conversion, merge-down, and flatten retain their staged reports and confirmation.

`where` remains one session publication for the one Studio `DocumentModel`, with
active layer ID, name, and scope unchanged by opening, moving, focusing, or
closing the tool. The CLI/AI boundary is explicit: `where` is observational only;
every CLI mutation or isolated render supplies its own document, layer ID/name,
frame, and scope, and never reads Studio or tool focus to fill an omitted target.
On multi-monitor desktops the window manager may clamp the requested starting
position to a visible work area, but the tool remains a separate resizable top-
level window and application content is not clipped by the Studio dock.

The artwork migration is deliberately not part of this gate. The Grok source is
owned by the follow-up migration task.
