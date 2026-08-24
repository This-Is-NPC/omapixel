# Studio model layer contract

`DocumentModel` is the QML-facing adapter for the v2 document model. The
renderer remains composite by default, while edits and active-cel reads use the
immutable `activeLayerId`.

## QML state

- `layers` exposes ordered `{id, name, visible, locked, opacity, mode, storage,
  shared, active}` records. IDs are stable; names are presentation text.
- `activeLayerId`, `activeLayerName`, `activeLayerLocked`, and
  `activeLayerStorage` describe the selected layer.
- `editScope` is `frame` or `all-frames`. A shared layer always resolves to its
  one cel, regardless of the selected frame or scope.
- `pickerScope` is `active` or `composite`.

## Layer dock and tool window

The Studio layer dock is the compact QML browser for the layer contract. Rows are
ordered bottom-to-top and show an isolated current-cel thumbnail, visibility,
lock state, editable name, storage badge, and opacity. Clicking or activating a
row changes the one active paint layer and opens or focuses the independent
non-modal Layer tool window. The checkbox at the left is a separate structural
selection: multiple checked rows may be moved, hidden, locked, or deleted, but
never painted together.

The Layer tool is a second top-level `QQuickWindow` in the same QML engine. It
shares the one `DocumentModel`, starts beside the Studio, and may be moved or
resized independently onto another monitor. Its `transientParent` associates it
with the Studio without making it a child or a second session. Tab reaches every
usable action; Space or Enter activates it; Escape closes the tool and returns
focus to the active row. Closing a consequence sheet returns focus to the tool
when it remains open.

The scope control is `current frame` or `all frames`; shared layers display the
all-frames rule and ignore the frame choice. Opacity is presented as both a
0-100 slider and a value. Merge-down, flatten-visible, and animated/shared
conversion first stage a consequence report and require an explicit confirmation
before the model commits the operation. Cancel leaves the document byte-for-byte
unchanged.

All dock and tool controls have a visible keyboard focus ring, a Tab order, Space/Enter
activation, and an `Accessible.name` describing the target and operation. The
layer list is scrollable at the constrained 900x560 Studio minimum;
the composed canvas and onion skin remain one C++ raster surface rather than one
QML item per layer.

## Editing rules

`paint`, `line`, `rect`, `fill`, `clearFrame`, `shift`, `flip`, and
`pastePixels` mutate only the active layer. `all-frames` is explicit; it never
leaks into another layer. A locked target is rejected without changing the
document and reports the localized `note.layerLocked` message.

`slotAt` and `copySelection` read the active cel. `compositeSlotAt` and
`pickSlot(..., true)` sample the visible compositor. The surface uses the
picker scope to make that distinction visible without creating a second raster
model in QML.

Whole-document snapshots include layer order, metadata, cels, palette, and the
active immutable ID. Stroke grouping still records only the first changed
snapshot. Reload and watcher adoption preserve the active ID when it exists;
missing IDs fall back deterministically to the previous position or the first
layer.
