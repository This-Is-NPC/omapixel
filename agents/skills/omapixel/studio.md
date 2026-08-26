# Studio Coordination

The Studio watches its open document. CLI writes appear live and become one
undoable external change, so inspect the session before writing.

## Discover Live Work

```bash
omapixel where
omapixel where /absolute/path/to/art.omapixel
```

The result reports each window's absolute path, `dirty` state, view, active
layer, clip, frame, scope, and selection. It is a point-in-time snapshot.

## Before Editing

1. Resolve the document to an absolute path.
2. Run `omapixel where <file>`.
3. If no Studio holds it, continue with normal file checks.
4. If `dirty` is `true`, explain that a CLI write may replace unsaved strokes
   and ask before proceeding.
5. If the user asks to edit what is selected, copy the reported clip, frame,
   layer ID, and coordinates into explicit CLI flags.
6. Run `where` again immediately before a consequential write if time passed.

`where` never supplies implicit defaults to another command. The following is
wrong even when the Studio currently displays the intended target:

```bash
omapixel paint art.omapixel --at 4,5 --slot R
```

Use the reported target explicitly:

```bash
omapixel paint art.omapixel --clip walk --frame 2 \
  --layer-id hero --scope frame --at 4,5 --slot R
```

## After Editing

```bash
omapixel check art.omapixel
omapixel text art.omapixel --clip walk --frame 2
omapixel render art.omapixel -o preview.png --clip walk --frame 2 --scale 8
```

Tell the user what target changed. Do not assume that Studio playback or focus
still points at the same frame after an external edit.
