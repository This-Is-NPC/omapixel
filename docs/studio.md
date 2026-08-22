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
│ File   Edit   Sprite   View                                  │
├───┬──────────────────────────────────────────┬───────────────┤
│ B │                                          │ ▼ Palette     │
│ E │                                          │ ▼ Preview     │
│ F │             the open frame               │ ▶ Sprite      │
│ I │                                          │ ▶ Reference   │
│ H │                                          │               │
│ ─ │                                          │               │
│ 1 │                                          │               │
│ 2 │                                          │               │
│ … │                                          │               │
├───┴──────────────────────────────────────────┴───────────────┤
│ clip  [idle] + −   idle          8 fps − +                   │
│ ▶  + frame  duplicate  ◂ ▸  delete    1  2  3  4             │
├──────────────────────────────────────────────────────────────┤
│ Tab draw with the keyboard  b e f i h tools  ; slot  ^S save │
│ 32×24  4,17  slot I  12×  idle · 4 frames · 8 fps     saved  │
└──────────────────────────────────────────────────────────────┘
```

Six places, each answering one question.

**The menu bar** has every command the studio has, named and grouped, with its
key beside it. Nothing is reachable only by knowing a shortcut, and nothing
common needs the menu twice — you read the key there and stop opening it.

**The tool strip** holds the two things that decide what the next press does:
the tool, and the colour. Its letters and digits are its keys, and the one in use
is marked in both halves — the colour you are drawing with is the same kind of
fact as the tool you are drawing with, and it used to be the only one of the two
you could not see without opening a panel. Hovering a number says which slot it
is and what colour.

**The dock** folds. A window is rarely tall enough for four panels at once, and
scrolling past a panel you are not using is worse than closing it. What you fold
stays folded while you work. Each header carries the fact you would have opened
it for — the palette's slot count, the sprite's size, the reference's opacity.

**The timeline** is two rows because it answers two questions: the top one picks
and names the clip and sets its speed, the bottom one is the sequence itself.

**The hint bar** is the keys, in inverse video, with what they do beside them —
the oldest affordance there is, and it survives because it works: the keys are
on screen while you use them, so nobody has to remember a list or go hunting in
a menu for something they will do forty times an hour. It changes with what you
are doing; while the colour leader is pending it says one thing, because saying
eight others would bury it. **View → Key hints** turns it off.

**The status line** is what is true right now: size, the colour being drawn with
as a colour rather than a letter, cursor, zoom, clip, frames, and whether it is
saved. The last message also lives there — what was
opened, what was saved, why something was refused. It is the first place to look
when a click seems to have done nothing.

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
| <kbd>Tab</kbd> | give the keyboard to the drawing, and bring the cursor out |
| <kbd>←</kbd> <kbd>↑</kbd> <kbd>→</kbd> <kbd>↓</kbd> | move the cursor one pixel · <kbd>Shift</kbd> for eight |
| <kbd>Return</kbd>, <kbd>x</kbd> | paint the one pixel under the cursor |
| <kbd>d</kbd> | draw-as-you-move mode · hold <kbd>1</kbd>–<kbd>0</kbd> to paint |
| <kbd>p</kbd> | pick-up-colours mode · a digit takes the colour under the cursor |
| <kbd>l</kbd> | pin a corner of a line · <kbd>Enter</kbd> or a digit draws it |
| <kbd>1</kbd>–<kbd>0</kbd> | use the colour on that digit, and paint with it |
| <kbd>Shift</kbd>+<kbd>1</kbd>–<kbd>0</kbd> | put the current colour on that digit |
| <kbd>p</kbd> | pick up the colour under the cursor |
| <kbd>c</kbd> | find a colour by name or hex, and put it on a number |
| <kbd>Shift</kbd>+<kbd>c</kbd> | replace every pixel of the colour in focus |
| <kbd>r</kbd> | Russian roulette — paint with a colour nobody chose |
| <kbd>;</kbd> then a letter | choose that palette slot, and paint the cursor with it |
| <kbd>Backspace</kbd> | erase at the cursor |
| <kbd>Esc</kbd> | put the cursor away |
| <kbd>,</kbd> <kbd>.</kbd> | previous and next frame |
| <kbd>+</kbd> <kbd>−</kbd> | zoom, 1 to 40 screen pixels per sprite pixel |
| wheel | scroll · <kbd>Ctrl</kbd>+wheel zooms |
| <kbd>o</kbd> | onion skin |
| <kbd>m</kbd> | the pixel mesh |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | undo |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Y</kbd> | redo |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | save |
| <kbd>q</kbd> | quit |

## Drawing without a mouse

The arrow keys walk a cursor across the drawing, one pixel at a time, eight with
<kbd>Shift</kbd>. <kbd>Return</kbd> or <kbd>x</kbd> draws at it with the selected
slot, <kbd>Backspace</kbd> erases, <kbd>Esc</kbd> puts it away.

<kbd>Tab</kbd> hands the keyboard back to the drawing from anywhere — a field, a
menu — and brings the cursor out if it was put away. Clicking on the drawing
does the same, and so does <kbd>Esc</kbd> from inside a text field.

A mouse is good at shapes and hopeless at placing one pixel exactly, which is
most of what pixel art is. The cursor is drawn in the accent, heavier than the
outline that follows the mouse, because it is a position the program is holding
on your behalf rather than one your hand is already on. Its coordinates take
over the status line while it is out, and a faint line runs the width and height
of the drawing through it. It is drawn in roughly the inverse of the pixel it
sits on rather than in a fixed colour: an accent that reads beautifully over the
background is invisible over a drawing that happens to use the accent's own
family of colours, and a cursor you cannot find is worse than no cursor — at 5× the cursor is a five-pixel square on a picture
made of five-pixel squares, findable only if you already know where it is.

Walking off the visible part of a zoomed drawing scrolls to follow, but only
when the cursor actually reaches the edge — recentring on every press makes the
drawing lurch under a cursor that is moving calmly across it.

## Modes

Three of them, and they all work the same way: one key turns it on, <kbd>Esc</kbd>
turns it off, and the hint bar says which one you are in. A toggle whose state
you have to remember is a toggle you get wrong.

### <kbd>d</kbd> — draw as you move

The arrows keep moving the cursor, and **while you hold a colour key** they
paint. Moving and drawing are the same gesture with and without your other hand
on a number. Holding <kbd>1</kbd> and leaning on an arrow draws a run in one go;
letting go ends it, and the whole run is **one** undo step.

<kbd>Enter</kbd> still paints the single pixel under the cursor, in the current
colour.

### <kbd>p</kbd> — pick up colours

The digits stop choosing a colour and start collecting one. Point the cursor at
a pixel, press a number, and that colour is on that number from now on. Most
colour choices while drawing are "that one, there", not "the twenty-second
slot". Nothing is painted while you are picking.

### <kbd>l</kbd> — straight lines

Each press pins a corner where the cursor is. Nothing is drawn yet: the canvas
shows the shape, the last leg following the cursor. A right angle is two corners
and then one press, rather than two separate lines whose ends you have to line
up by hand.

<kbd>Enter</kbd> draws it in the current colour, a digit draws it in that
digit's colour, <kbd>Esc</kbd> throws it away. However many corners it had, it
is one undo step — it was one line as far as you were concerned.

## Choosing a colour from the keyboard

The ten colours live in the tool strip, under the tools, each showing its colour
and its number, the one in use outlined like the active tool. Clicking one
chooses it; it does not paint, because a hand already on the mouse will draw by
clicking the canvas next.

**The number keys hold ten colours.** <kbd>1</kbd>–<kbd>0</kbd> uses one, and
paints the pixel under the cursor with it — choosing a colour and applying it is
one press, not two. <kbd>Shift</kbd> and a digit puts the current colour on that
digit. A palette of thirty-three slots is a wall to hunt through; the handful you
are using at this moment is small, and it changes as the drawing does.

They start on the first ten slots of the palette, and reset when another
document is opened — digits pointing at slots that are no longer there would be
worse than digits pointing at nothing. Each swatch in the palette panel carries
its digit in the corner: a shortcut you cannot see is one only its author
remembers.

**<kbd>;</kbd> then the slot's letter** reaches any of them.

## A colour you do not know yet

<kbd>c</kbd>, or **Sprite → Choose a colour…**, opens a panel you can search.
Type a name — `teal`, `coral`, `slategray` — and the matches appear with their
swatches; type a hex and it comes back first, under what you typed, so there is
no separate field that only takes hexes. The names are the hundred and
forty-eight SVG ones everybody has already agreed on.

Somebody who wants "a teal" does not want to compose one out of three numbers,
and a spectrum is only useful once you already know roughly where you are going.

**Three keys, start to finish.** <kbd>c</kbd> opens it with the keyboard already
in the search box. Type, and <kbd>↑</kbd> <kbd>↓</kbd> walk the matches without
leaving the box. <kbd>Enter</kbd> settles on one. Then **press a number key**
and the colour is on it — the panel closes and you are drawing with it.

The two steps are not ceremony: the digits are needed for typing a hex right up
until the colour is chosen, and one key cannot mean two things at once. The row
of ten at the bottom shows what each number currently holds, and clicking one
does the same as pressing it.

The panel only chooses a colour, and it **adds** it: the number keys are a set of
colours to draw *with*, and the palette is what the drawing is *made of*. Putting
a colour on a key that already held one points that key somewhere new; it does
not repaint anything.

That distinction matters here more than in most editors. A slot is not a swatch —
every pixel drawn with it refers to it, so changing a slot's colour repaints all
of them at once. It is the best thing about this format and the worst thing to do
by accident: you reach for a nicer blue and the sky you painted last week changes
with it. The one control that does it on purpose is the colour field in the
**Palette** panel, and it says so underneath.

## Replacing a colour

<kbd>Shift</kbd>+<kbd>c</kbd>, or **Sprite → Replace this colour…**, opens the
same search panel pointed at the colour in focus — what the cursor is standing
on, and failing that what you are drawing with. <kbd>Enter</kbd> repaints every
pixel of it, and the panel says how many there are before you commit.

**Every frame of every clip.** A colour belongs to the document, not to the
frame you happen to be looking at; replacing it in one frame of twelve leaves an
animation that flickers between two colours, which is never what anybody meant.

It works by moving those pixels onto a slot that holds the new colour, not by
recolouring the slot they were on. The two look identical here and are not: if
some other part of the drawing shares that slot on purpose, it keeps its colour.
The emptied slot is dropped from the palette, so replacing repeatedly does not
silt it up with entries no pixel refers to.

## Russian roulette

<kbd>r</kbd>, the `?` at the bottom of the tool strip, or **Sprite → Russian
roulette**. It paints the pixel under the cursor with a colour nobody chose —
drawn out of the whole of RGB, not out of the palette, because picking from what
is already in the document would be a shuffle, and a shuffle is not a gamble.

Like every other way a colour arrives here it **adds** a slot: what you are
gambling on is which colour you get, not which of the ones already in the drawing
gets ruined. And it is one undo step, so the gamble is reversible even if the
name says otherwise.

## Modes

Three of them, and they all work the same way: one key turns it on, <kbd>Esc</kbd>
turns it off, and the hint bar says which one you are in. A toggle whose state
you have to remember is a toggle you get wrong.

### <kbd>d</kbd> — draw as you move

The arrows keep moving the cursor, and **while you hold a colour key** they
paint. Moving and drawing are the same gesture with and without your other hand
on a number. Holding <kbd>1</kbd> and leaning on an arrow draws a run in one go;
letting go ends it, and the whole run is **one** undo step.

<kbd>Enter</kbd> still paints the single pixel under the cursor, in the current
colour.

### <kbd>p</kbd> — pick up colours

The digits stop choosing a colour and start collecting one. Point the cursor at
a pixel, press a number, and that colour is on that number from now on. Most
colour choices while drawing are "that one, there", not "the twenty-second
slot". Nothing is painted while you are picking.

### <kbd>l</kbd> — straight lines

Each press pins a corner where the cursor is. Nothing is drawn yet: the canvas
shows the shape, the last leg following the cursor. A right angle is two corners
and then one press, rather than two separate lines whose ends you have to line
up by hand.

<kbd>Enter</kbd> draws it in the current colour, a digit draws it in that
digit's colour, <kbd>Esc</kbd> throws it away. However many corners it had, it
is one undo step — it was one line as far as you were concerned.

## Choosing a colour from the keyboard

**The number keys hold ten colours.** <kbd>1</kbd>–<kbd>0</kbd> uses one, and
paints the pixel under the cursor with it — choosing a colour and applying it is
one press, not two. <kbd>Shift</kbd> and a digit puts the current colour on that
digit. A palette of thirty-three slots is a wall to hunt through; the handful you
are using at this moment is small, and it changes as the drawing does.

They start on the first ten slots of the palette, and reset when another
document is opened — digits pointing at slots that are no longer there would be
worse than digits pointing at nothing. Each swatch in the palette panel carries
its digit in the corner: a shortcut you cannot see is one only its author
remembers.

**<kbd>;</kbd> then the slot's letter** reaches any of them.

## A colour you do not know yet

<kbd>c</kbd>, or **Sprite → Choose a colour…**, opens a panel you can search.
Type a name — `teal`, `coral`, `slategray` — and the matches appear with their
swatches; type a hex and it comes back first, under what you typed, so there is
no separate field that only takes hexes. The names are the hundred and
forty-eight SVG ones everybody has already agreed on.

Somebody who wants "a teal" does not want to compose one out of three numbers,
and a spectrum is only useful once you already know roughly where you are going.

Then say **where it goes**: one of the palette's slots, to replace its colour, or
a letter that is not in the palette yet, to add one. A colour with nowhere to
live is not a choice — in this format colours live in slots, and every pixel
drawn with that slot changes with it.

**It never needs the mouse.** <kbd>c</kbd> opens it with the keyboard already in
the search box; <kbd>↑</kbd> <kbd>↓</kbd> walk the matches without leaving the
box, so searching and choosing are one gesture rather than two; <kbd>Tab</kbd>
moves to the destination slot and back; <kbd>Enter</kbd> applies from either
field; <kbd>Esc</kbd> closes. A panel that opens on a key and then makes you
reach for the mouse to type in it has given the key back. It sets the current slot, and paints the
pixel under the cursor with it if the cursor is out.

Every letter is already a tool or a toggle, so the letters that name palette
slots had nowhere to go: pressing `b` picks the pencil, not slot B. The
semicolon says "the next key names a colour", and for that one press the letters
are colours again. The status line says so while it is waiting, because a mode
with nothing on screen to announce it is a trap.

Case matters — <kbd>;</kbd> <kbd>B</kbd> and <kbd>;</kbd> <kbd>b</kbd> are
different slots, and a palette can use both. <kbd>;</kbd> <kbd>.</kbd> is the
empty slot, the same thing the eraser draws. <kbd>Esc</kbd> backs out.

Frames moved to <kbd>,</kbd> and <kbd>.</kbd>, where other sprite editors put
them. The arrows are worth more on the canvas, and stepping through frames is
not something you do with your hand on the drawing.

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

Under it is the palette, one swatch per slot, in the document's own order, **nine
rows at a time**. A palette can run to hundreds of slots, and letting it push the
panels below it off the bottom of the window costs more than it gives: the slots
you are working with are at the top, the rest are a scroll or one click on
*show all* away. Click
a swatch to draw with it. The `×` at the front is transparency — the same thing
the eraser draws. The field at the bottom is the colour of the **selected** slot:
type a new `#RRGGBB` and every pixel using that letter changes at once,
everywhere in the document. That is the whole point of storing letters instead of
colours — see [the format](format.md).

## Files

Everything is in the **File** menu, and everything uses the system's own file
dialog rather than a path you type.

| | |
|---|---|
| New… | <kbd>Ctrl</kbd>+<kbd>N</kbd> — size presets, or your own numbers |
| Open… | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save · Save as… | <kbd>Ctrl</kbd>+<kbd>S</kbd> · <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Export PNG… | <kbd>Ctrl</kbd>+<kbd>E</kbd> — the open frame, at a scale you pick |
| Export sprite sheet… | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>E</kbd> — every frame side by side |

Export goes through the same `render::` call `omapixel render` uses, so a
picture out of the studio and a picture out of the command line are the same
picture. The dialog says the pixel size you will get before you commit to it.

## The dock: palette, preview, sprite, reference

**Sprite** — presets from 16×16 to 128×128, or type columns and rows. `resize`
keeps the drawing centred and is greyed out when the numbers match what you
already have; it warns before cropping drawn pixels, on the button, with the
count.

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

## Looking at the window without a screen

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  OMAPIXEL_SHOT=/tmp/studio.png omapixel-studio drawing.json
```

Renders the window to a PNG and exits. The studio was the one part of this
project that could not be inspected without a display — a layout change had to
be described and taken on trust. This is how the layout above was checked.

`OMAPIXEL_SHOT_SHEET=colour` opens that panel first — `replace`, `new` and
`export` work too. A popup does not appear in a window grab, so a panel that only exists once
opened cannot otherwise be looked at without a display.

`OMAPIXEL_DEBUG_INPUT=1` logs every wheel and gesture event as it reaches the
window, and again from the handler that acts on it, straight to stderr. Input
that never arrives and input that arrives and is ignored look identical from the
outside; this tells them apart.
