# The studio

```bash
mise run studio              # an empty 32×24 document
mise run studio heart.json   # open a file
```

The window is the same core as [the command line](cli.md) behind a mouse. It
edits the same documents and obeys the same rules; there is nothing it can do
that the terminal cannot, and nothing the terminal can do that it refuses.

## The layout

```
┌──────────────────────────────────────────────────────────────┐
│ omapixel  heart.json · 2 clips      tools · zoom · save      │
├──────────┬───────────────────────────────────┬───────────────┤
│ clips    │                                   │ size          │
│ palette  │          the open frame           │ file          │
│          │                                   │ reference     │
│          │                                   │ true size     │
├──────────┴───────────────────────────────────┴───────────────┤
│ ▶  + frame  duplicate  ‹ ›  delete   8 fps − +   1 2 3 4     │
└──────────────────────────────────────────────────────────────┘
```

The note beside the title is the last thing that happened — what was opened,
what was saved, why something was refused. It is the first place to look when a
click seems to have done nothing.

## Drawing

**Left button** draws with the selected slot. **Right button** erases, whatever
tool is selected — so you rarely need to switch to the eraser and back.

Dragging paints the **path**, not the samples. At a fast drag the mouse skips
cells; painting only where the events landed would leave a dotted line.

| Tool | Key | |
|---|---|---|
| pencil | <kbd>b</kbd> | one pixel at a time |
| hand | <kbd>h</kbd> | drags the drawing instead of drawing on it |
| eraser | <kbd>e</kbd> | draws emptiness |
| bucket | <kbd>f</kbd> | floods the contiguous run under the cursor |
| picker | <kbd>i</kbd> | takes the slot under the cursor, then switches back to the pencil |

## Moving around the drawing

**The wheel scrolls** the drawing, both axes, so a trackpad's two fingers work
the way they do everywhere else. Shift with a one-axis wheel scrolls sideways.

**Ctrl and the wheel zooms**, about the pixel under the cursor — so what you
were looking at stays where it was, instead of sliding off while you chase it.
**Alt and the wheel** does the same, and **two fingers pinching** on a trackpad
does it with no modifier at all. Three ways because a compositor or one of its
plugins can take a modifier for itself before any window sees it, and when that
happens the application cannot tell: the event simply never arrives. The `−`
`+` chips in the head and the <kbd>+</kbd> <kbd>−</kbd> keys never involve a
modifier either.

**The hand tool** (<kbd>h</kbd>, or the `h hand` chip) drags the drawing with
the left button, for a mouse or trackpad with no middle button. **The middle
button** drags at any time, whatever tool is chosen.

Once the drawing is bigger than the pane, **scrollbars** appear along the bottom
and the right. Drag the handle, or click anywhere on the track to jump there.
The handle's length is the fraction of the drawing you are looking at, so it
says where you are as much as it moves you.

To reach a specific place, use the **overview**: the 1× tile under *true size*
in the right rail. While the drawing is larger than the pane it draws a frame
around the part you are looking at, and clicking or dragging in it takes the
view there. It is the whole picture at once, which is the thing a zoomed pane
stops being.

In the head, `−` `12× fit` `+`. The middle one fits the whole drawing in the
pane, and hands the view back to the window: after that it re-fits when the
window is resized, until the next time you zoom or pan.

A document opens fitted. A 160×90 picture does not fit a pane at 12×, and
opening scrolled into the middle of one looks like a broken window rather than a
zoomed one.

The pixel mesh switches itself off below 4×: at that size it has more lines than
the drawing has pixels, and it stops measuring the grid and starts hiding it.

## Keys

| | |
|---|---|
| <kbd>b</kbd> <kbd>e</kbd> <kbd>f</kbd> <kbd>i</kbd> <kbd>h</kbd> | pencil, eraser, bucket, picker, hand |
| <kbd>Space</kbd> | play and pause the clip |
| <kbd>←</kbd> <kbd>→</kbd> | previous and next frame |
| <kbd>+</kbd> <kbd>−</kbd> | zoom, 1 to 40 screen pixels per sprite pixel |
| wheel | scroll · <kbd>Ctrl</kbd>+wheel zooms |
| <kbd>o</kbd> | onion skin |
| <kbd>m</kbd> | the pixel mesh |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | undo |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Y</kbd> | redo |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | save |
| <kbd>q</kbd> | quit |

## Undo

<kbd>Ctrl</kbd>+<kbd>Z</kbd> and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>,
or the `↶` `↷` chips in the head. Eighty steps deep.

**A drag is one step**, not one per pixel it crossed — needing forty presses to
take back one stroke is the same as having no undo. A stroke that changes
nothing files no step at all, so the first press is never a press that appears
to be ignored.

**It covers structure, not just pixels**: adding and deleting clips and frames,
resizing, recolouring a palette slot, and `new`. Undo that saves you from a
stray pixel but not from deleting the wrong clip is undo for the cheap mistakes
and not the expensive ones.

Editing after undoing drops the redo branch, as everywhere else. Opening another
file clears the history — the old document's steps do not apply to the new one.

## Left: clips and palette

The clip list is the document's clips in order; click one to open it. **+ clip**
adds, **delete** removes — except the last one, which stays, because a document
with no clips cannot be drawn or reopened. The field below renames the open clip.

Under it is the palette, one swatch per slot, in the document's own order. Click
a swatch to draw with it. The `×` at the front is transparency — the same thing
the eraser draws. The field at the bottom is the colour of the **selected** slot:
type a new `#RRGGBB` and every pixel using that letter changes at once,
everywhere in the document. That is the whole point of storing letters instead of
colours — see [the format](format.md).

## Right: size, file, reference, true size

**size** — presets from 16×16 to 128×128, or type columns and rows. `resize`
keeps the drawing centred and is greyed out when the numbers match what you
already have; it warns before cropping drawn pixels. `new` starts an empty
document at that size and discards what is open, so save first.

**file** — where the document lives, and `open` to load another. Saving is
<kbd>Ctrl</kbd>+<kbd>S</kbd> or the **save** chip in the head.

**reference** — the path to an image to trace over: a photo, a mockup, art you
are matching. The slider row sets its opacity from 0 to 100%, and **behind**
flips it between under the drawing and over it. Under is for copying a shape;
over is for checking one you have already drawn.

**true size** — the open frame at 1×, 2× and 3×, unzoomed. Pixel art is judged at
the size it will be seen, and a sprite that reads beautifully at 12× can be
unreadable at 1×. Look here before deciding a frame is finished.

A scale that will not fit the rail is not shown: a cropped centre of a sprite is
not "true size", it is a different picture. A 160-column document therefore
offers 1× only.

## Bottom: the timeline

Every frame of the open clip, in order, as thumbnails; click one to open it.

| | |
|---|---|
| ▶ | play the clip at its fps, looping |
| **+ frame** | a blank frame after the current one |
| **duplicate** | a copy of the current one — the usual way to start the next frame of a walk |
| **‹ ›** | move the current frame earlier or later |
| **delete** | remove it; a clip keeps its last frame |
| **fps − +** | how fast this clip plays. It belongs to the clip, not the document, so a slow blink and a fast run live in one file |

## Onion skin

<kbd>o</kbd> draws the previous frame faded underneath the current one. It is for
seeing **movement** — whether a limb travelled the same distance between frames 2
and 3 as it did between 1 and 2 — which is invisible when you look at one frame
at a time and obvious when you look through them.

## It follows the omarchy theme

The window reads the active omarchy theme's colours and Hyprland's corner
rounding, and follows a theme change live — `omarchy theme set <name>` recolours
it without a restart. On a machine that is not running omarchy it opens on its
built-in defaults. The details are in the [README](../README.md#it-wears-the-omarchy-theme).

The desktop theme never touches the **document's** palette. Your art does not
change colour because you changed themes.
