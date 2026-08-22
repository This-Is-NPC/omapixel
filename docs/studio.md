# The studio

```bash
mise run studio              # an empty document, 32×24 unless you said otherwise
mise run studio heart.json   # open a file
```

The window is the same core as [the command line](cli.md) behind a mouse. It
edits the same documents and obeys the same rules; there is nothing it can do
that the terminal cannot, and nothing the terminal can do that it refuses.

## The layout

```
┌──────────────────────────────────────────────────────────────┐
│ File  Edit  Sprite  View     omapixel     heart.json         │
├───┬──────────────────────────────────────────┬───────────────┤
│ B │                                          │ ▼ Palette  17 │
│ E │                                          │ ▼ Preview  12×│
│ F │          the open frame                  │ ▶ Sprite      │
│ I │                                          │ ▶ Reference   │
│ H │                                          │               │
│ ─ │                                          │               │
│ 1 │                                          │               │
│ 2 │                                          │               │
│ … │                                          │               │
│   ├──────────────────────────────────────────┤               │
│ ? │ arrows draw · b e f i h tools · ^S save  │               │
├───┴──────────────────────────────────────────┴───────────────┤
│ clip  [idle] + −   idle          8 fps − +                   │
│ ▶  + frame  duplicate  ◂ ▸  delete   1  2  3  4              │
├──────────────────────────────────────────────────────────────┤
│ 32×24  4,17  slot I  12×  idle · 4 frames · 8 fps    saved   │
└──────────────────────────────────────────────────────────────┘
```

Six places, each answering one question.

**The menu bar** has every command the studio has, named and grouped, with its
key beside it. Nothing is reachable only by knowing a shortcut, and nothing
common needs the menu twice, because you read the key there and stop opening it.

**The tool strip** holds the two things that decide what the next press does:
the tool, and the colour. Its letters and digits are its keys, and the one in use
is marked in both halves. The colour you are drawing with is the same kind of
fact as the tool you are drawing with, and it used to be the only one of the two
you could not see without opening a panel. Hovering a number says which slot it
is and what colour.

**The dock** folds. A window is rarely tall enough for four panels at once, and
scrolling past a panel you are not using is worse than closing it. What you fold
stays folded while you work. Each header carries the fact you would have opened
it for: the palette's slot count, the sprite's size, the reference's opacity.

**The timeline** is two rows because it answers two questions: the top one picks
and names the clip and sets its speed, the bottom one is the sequence itself.

**The hint bar** sits under the drawing, in the canvas's own footer rather than
along the bottom of the window: it is about what your hands are doing on the
canvas, so it belongs beside the canvas. It is the keys, in inverse video, with
what they do beside them,
the oldest affordance there is, and it survives because it works: the keys are
on screen while you use them, so nobody has to remember a list or go hunting in
a menu for something they will do forty times an hour. It changes with what you
are doing; while the colour leader is pending it says one thing, because saying
eight others would bury it. It shows whatever you have **bound**, not what is
printed in this page. **View → Key hints** turns it off, and so does
`hints = false` in the [config file](configuration.md).

**The status line** is what is true right now: size, the colour being drawn with
as a colour rather than a letter, cursor, zoom, clip, frames, and whether it is
saved. The last message also lives there: what was
opened, what was saved, why something was refused. It is the first place to look
when a click seems to have done nothing.

## Drawing

**Left button** draws with the selected slot. **Right button** erases, whatever
tool is selected, so you rarely need to switch to the eraser and back.

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

**Ctrl and the wheel zooms**, about the pixel under the cursor, so what you
were looking at stays where it was, instead of sliding off while you chase it.
**Alt and the wheel** does the same, and **two fingers pinching** on a trackpad
does it with no modifier at all. Three ways because a compositor or one of its
plugins can take a modifier for itself before any window sees it, and when that
happens the application cannot tell: the event simply never arrives. The bare
<kbd>+</kbd> and <kbd>−</kbd> keys never involve a modifier either.

**The hand tool** (<kbd>h</kbd>, or `H` in the tool strip) drags the drawing with
the left button, for a mouse or trackpad with no middle button. **The middle
button** drags at any time, whatever tool is chosen.

Once the drawing is bigger than the pane, **scrollbars** appear along the bottom
and the right. Drag the handle, or click anywhere on the track to jump there.
The handle's length is the fraction of the drawing you are looking at, so it
says where you are as much as it moves you.

To reach a specific place, use the **overview**: the 1× tile in the **Preview**
panel. While the drawing is larger than the pane it draws a frame around the
part you are looking at, and clicking or dragging in it takes the view there. It
is the whole picture at once, which is the thing a zoomed pane stops being.

**View → Fit** (<kbd>Ctrl</kbd>+<kbd>0</kbd>) fits the whole drawing in the
pane, and hands the view back to the window: after that it re-fits when the
window is resized, until the next time you zoom or pan.

A document opens fitted, or at whatever `canvas.zoom` says in the
[config file](configuration.md). A 160×90 picture does not fit a pane at 12×,
and opening scrolled into the middle of one looks like a broken window rather
than a zoomed one.

The pixel mesh switches itself off below 4×: at that size it has more lines than
the drawing has pixels, and it stops measuring the grid and starts hiding it.

## Keys

Every key below is a default. They are all in one file and all rebindable: see
[Settings and keys](configuration.md), or run `omapixel config write` to get
the annotated copy and change the ones you disagree with. The hint bar under the
drawing shows whatever you bound, not what is printed here.

| | |
|---|---|
| <kbd>b</kbd> <kbd>e</kbd> <kbd>f</kbd> <kbd>i</kbd> <kbd>h</kbd> | pencil, eraser, bucket, picker, hand |
| <kbd>Space</kbd> | play and pause the clip |
| <kbd>Tab</kbd> | walk the window's controls |
| <kbd>F10</kbd>, <kbd>Alt</kbd>+<kbd>F</kbd> … | the menu bar |
| <kbd>Esc</kbd> | back to the drawing, from anywhere |
| <kbd>[</kbd> <kbd>]</kbd> | previous and next clip |
| <kbd>Shift</kbd>+<kbd>,</kbd> <kbd>Shift</kbd>+<kbd>.</kbd> | move this frame earlier or later |
| <kbd>←</kbd> <kbd>↑</kbd> <kbd>→</kbd> <kbd>↓</kbd> | move the cursor one pixel · <kbd>Shift</kbd> for `canvas.big_step`, eight by default |
| <kbd>Return</kbd>, <kbd>x</kbd> | paint the one pixel under the cursor |
| <kbd>d</kbd> | draw-as-you-move mode · hold <kbd>1</kbd>–<kbd>0</kbd> to paint |
| <kbd>p</kbd> | pick-up-colours mode · a digit takes the colour under the cursor |
| <kbd>l</kbd> | pin a corner of a line · <kbd>Enter</kbd> or a digit draws it |
| <kbd>1</kbd>–<kbd>0</kbd> | use the colour on that digit, and paint with it |
| <kbd>Shift</kbd>+<kbd>1</kbd>–<kbd>0</kbd> | put the current colour on that digit |
| <kbd>c</kbd> | find a colour by name or hex, and put it on a number |
| <kbd>Shift</kbd>+<kbd>c</kbd> | replace every pixel of the colour in focus |
| <kbd>r</kbd> | Russian roulette: paint with a colour nobody chose |
| <kbd>;</kbd> then a letter | choose that palette slot, and paint the cursor with it |
| <kbd>Backspace</kbd> | erase at the cursor |
| <kbd>Esc</kbd> | put the cursor away |
| <kbd>,</kbd> <kbd>.</kbd> | previous and next frame |
| <kbd>+</kbd> <kbd>−</kbd> | zoom, 1 to 40 screen pixels per sprite pixel |
| wheel | scroll · <kbd>Ctrl</kbd>+wheel zooms |
| <kbd>o</kbd> <kbd>m</kbd> | onion skin · the pixel mesh |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> · <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | undo · redo |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> · <kbd>Ctrl</kbd>+<kbd>E</kbd> | save · export a PNG |
| <kbd>Ctrl</kbd>+<kbd>Q</kbd> | quit |

## Drawing without a mouse

The arrow keys walk a cursor across the drawing, one pixel at a time, eight with
<kbd>Shift</kbd>, or whatever `canvas.big_step` says. <kbd>Return</kbd> or <kbd>x</kbd> draws at it with the selected
slot, <kbd>Backspace</kbd> erases, <kbd>Esc</kbd> puts it away.

Nothing here needs a pointer. <kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd>
walk every control in the window (tools, colours, panel headers, the timeline,
the buttons in a panel) and <kbd>Space</kbd> or <kbd>Enter</kbd> works whatever
the focus is on. <kbd>F10</kbd> puts the keyboard on the menu bar, and
<kbd>Alt</kbd> with a menu's underlined letter opens it directly.

<kbd>Esc</kbd> hands the keyboard back to the drawing from anywhere: a button,
a field, a menu. Clicking on the drawing does the same. It is one key, and it
always means the same thing, which is what you want from the key you reach for
when you are lost.

A mouse is good at shapes and hopeless at placing one pixel exactly, which is
most of what pixel art is. The cursor is drawn in the accent, heavier than the
outline that follows the mouse, because it is a position the program is holding
on your behalf rather than one your hand is already on. Its coordinates take
over the status line while it is out, and a faint line runs the width and height
of the drawing through it. It is drawn in roughly the inverse of the pixel it
sits on rather than in a fixed colour: an accent that reads beautifully over the
background is invisible over a drawing that happens to use the accent's own
family of colours, and a cursor you cannot find is worse than no cursor. At 5×
it is a five-pixel square on a picture made of five-pixel squares, findable only
if you already know where it is.

Walking off the visible part of a zoomed drawing scrolls to follow, but only
when the cursor actually reaches the edge. Recentring on every press makes the
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
is one undo step, because it was one line as far as you were concerned.

## Choosing a colour from the keyboard

The ten colours live in the tool strip, under the tools, each showing its colour
and its number, the one in use outlined like the active tool. Clicking one
chooses it; it does not paint, because a hand already on the mouse will draw by
clicking the canvas next.

**The number keys hold ten colours.** <kbd>1</kbd>–<kbd>0</kbd> uses one, and
paints the pixel under the cursor with it, so choosing a colour and applying it
is one press rather than two. <kbd>Shift</kbd> and a digit puts the current colour on that
digit. A palette of thirty-three slots is a wall to hunt through; the handful you
are using at this moment is small, and it changes as the drawing does.

They start on the first ten slots of the palette, and reset when another
document is opened, because digits pointing at slots that are no longer there
would be worse than digits pointing at nothing. Each swatch in the palette panel carries
its digit in the corner: a shortcut you cannot see is one only its author
remembers.

**<kbd>;</kbd> then the slot's letter** reaches any of them.

Every letter is already a tool or a toggle, so the letters that name palette
slots had nowhere to go: pressing `b` picks the pencil, not slot B. The
semicolon says "the next key names a colour", and for that one press the letters
are colours again. The status line says so while it is waiting, because a mode
with nothing on screen to announce it is a trap.

Case matters: <kbd>;</kbd> <kbd>B</kbd> and <kbd>;</kbd> <kbd>b</kbd> are
different slots, and a palette can use both. <kbd>;</kbd> <kbd>.</kbd> is the
empty slot, the same thing the eraser draws. <kbd>Esc</kbd> backs out.

## A colour you do not know yet

<kbd>c</kbd>, or **Sprite → Choose a colour…**, opens a panel you can search.
Type a name such as `teal`, `coral` or `slategray`, and the matches appear with their
swatches; type a hex and it comes back first, under what you typed, so there is
no separate field that only takes hexes. The names are the hundred and
forty-eight SVG ones everybody has already agreed on.

Somebody who wants "a teal" does not want to compose one out of three numbers,
and a spectrum is only useful once you already know roughly where you are going.

**Three keys, start to finish.** <kbd>c</kbd> opens it with the keyboard already
in the search box. Type, and <kbd>↑</kbd> <kbd>↓</kbd> walk the matches without
leaving the box. <kbd>Enter</kbd> settles on one. Then **press a number key**
and the colour is on it. The panel closes and you are drawing with it.

The two steps are not ceremony: the digits are needed for typing a hex right up
until the colour is chosen, and one key cannot mean two things at once. The row
of ten at the bottom shows what each number currently holds, and clicking one
does the same as pressing it.

The panel only chooses a colour, and it **adds** it: the number keys are a set of
colours to draw *with*, and the palette is what the drawing is *made of*. Putting
a colour on a key that already held one points that key somewhere new; it does
not repaint anything.

That distinction matters here more than in most editors. A slot is not a swatch:
every pixel drawn with it refers to it, so changing a slot's colour repaints all
of them at once. It is the best thing about this format and the worst thing to do
by accident: you reach for a nicer blue and the sky you painted last week changes
with it. The one control that does it on purpose is the colour field in the
**Palette** panel, and it says so underneath.

## Replacing a colour

<kbd>Shift</kbd>+<kbd>c</kbd>, or **Sprite → Replace this colour…**, opens the
same search panel pointed at the colour in focus: what the cursor is standing
on, and failing that what you are drawing with.

**<kbd>Enter</kbd> repaints this frame; <kbd>Shift</kbd>+<kbd>Enter</kbd>
repaints every frame of every clip.** Both are wanted and neither is the obvious
default, since doing one frame of an animation leaves it flickering between two
colours, and doing all twelve when you meant one is a bigger mistake and a
quieter one, so they are one keystroke apart and the panel gives both counts
before you commit to either. The buttons under it say the same thing for a hand
on the mouse.

It works by moving those pixels onto a slot that holds the new colour, not by
recolouring the slot they were on. The two look identical here and are not: if
some other part of the drawing shares that slot on purpose, it keeps its colour.
The emptied slot is dropped from the palette, so replacing repeatedly does not
silt it up with entries no pixel refers to.

## Russian roulette

<kbd>r</kbd>, the `?` at the bottom of the tool strip, or **Sprite → Russian
roulette**. It paints the pixel under the cursor with a colour nobody chose,
drawn out of the whole of RGB, not out of the palette, because picking from what
is already in the document would be a shuffle, and a shuffle is not a gamble.

Like every other way a colour arrives here it **adds** a slot: what you are
gambling on is which colour you get, not which of the ones already in the drawing
gets ruined. And it is one undo step, so the gamble is reversible even if the
name says otherwise.

## Undo

<kbd>Ctrl</kbd>+<kbd>Z</kbd> and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>,
or **Edit → Undo · Redo**. Eighty steps deep, or `history.depth` in the
[config file](configuration.md). Each step is a whole document, so how deep is
worth having depends on how big the ones you draw are.

**A drag is one step**, not one per pixel it crossed. Needing forty presses to
take back one stroke is the same as having no undo. A stroke that changes
nothing files no step at all, so the first press is never a press that appears
to be ignored.

**It covers structure, not just pixels**: adding and deleting clips and frames,
resizing, recolouring a palette slot, and `new`. Undo that saves you from a
stray pixel but not from deleting the wrong clip is undo for the cheap mistakes
and not the expensive ones.

Editing after undoing drops the redo branch, as everywhere else. Opening another
file clears the history, because the old document's steps do not apply to the
new one.

## Files

Everything is in the **File** menu, and everything uses the system's own file
dialog rather than a path you type.

| | |
|---|---|
| New… | <kbd>Ctrl</kbd>+<kbd>N</kbd>, size presets or your own numbers |
| Open… | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save · Save as… | <kbd>Ctrl</kbd>+<kbd>S</kbd> · <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Export PNG… | <kbd>Ctrl</kbd>+<kbd>E</kbd>, the open frame at a scale you pick |
| Export sprite sheet… | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>E</kbd>, every frame side by side |

New, Open, Quit and closing the window protect a changed document with the same
**Save / Discard / Cancel** choice. Save continues only after the write succeeds;
an untitled document asks for a path first. Cancel leaves the document, path,
history and unsaved state untouched.

Export goes through the same `render::` call `omapixel render` uses, so a
picture out of the studio and a picture out of the command line are the same
picture. The dialog says the pixel size you will get before you commit to it.

## The dock: palette, preview, sprite, reference

Four panels down the right, each folding away from its header. A window is
rarely tall enough for all four, and scrolling past one you are not using is
worse than closing it, and what you fold stays folded while you work. Each header
carries the fact you would have opened it for: the palette's slot count, the
preview's zoom, the sprite's size, the reference's opacity.

**Palette**: one swatch per slot, in the document's own order, **nine rows at a
time**. A palette can run to hundreds of slots, and letting it push the panels
below it off the bottom of the window costs more than it gives: the slots you
are working with are at the top, and *show all* is one click away. Click a
swatch to draw with it; the `×` at the front is transparency, the same thing the
eraser draws. Each swatch carries its digit in the corner when a number key
holds it.

The field underneath is the colour of the **selected** slot: type a new
`#RRGGBB` and every pixel using that letter changes at once, everywhere in the
document. It says so underneath, because it is the one control in the window
that repaints art you cannot see. That is also the whole point of storing
letters instead of colours; see [the format](format.md).

**Preview**: the open frame at 1×, 2× and 3×, unzoomed. Pixel art is judged at
the size it will be seen, and a sprite that reads beautifully at 12× can be
unreadable at 1×. Look here before deciding a frame is finished.

A scale that will not fit the dock is not shown: a cropped centre of a sprite is
not true size, it is a different picture. A 160-column document therefore offers
1× only. That 1× tile doubles as the **overview** whenever the drawing is bigger
than the pane. It frames the part you are looking at, and clicking or dragging
in it takes the view there. It is the whole picture at once, which is the thing
a zoomed pane stops being.

**Sprite**: presets from 16×16 to 128×128, or type columns and rows. `resize`
keeps the drawing centred and is greyed out when the numbers match what you
already have; it warns before cropping drawn pixels, on the button, with the
count.

**Reference**: an image to trace over, a photo, a mockup, art you are matching.
The row of `0% 25% 50% 75% 100%` sets its opacity, and one chip flips it between
**behind** the drawing and **on top**. Behind is for copying a shape; on top is
for checking one you have already drawn.

## Bottom: the timeline

Two rows, because they answer two questions.

The **top row** is the clip: every clip in the document as a chip, click one to
open it; `+` adds one and `−` removes it, except the last, because a document
with no clips cannot be drawn or reopened. The field beside them renames the
open clip, and `fps − +` sets how fast it plays. Speed belongs to the clip and
not to the document, so a slow blink and a fast run live in one file.

The **bottom row** is the sequence itself: every frame of the open clip, in
order, as thumbnails; click one to open it. Each thumbnail is the real frame
drawn small rather than an icon. The question a timeline answers is "is the
movement right?", and it only answers it if you can see the movement in it.

| | |
|---|---|
| ▶ | play the clip at its fps |
| **+ frame** | a blank frame after the current one |
| **duplicate** | a copy of the current one, the usual way to start the next frame of a walk |
| **◂ ▸** | move the current frame earlier or later |
| **delete** | remove it; a clip keeps its last frame |

**View → Loop playback** decides what ▶ does at the end. Looping is right for
judging movement, whether a limb travels the same distance between frames, and
wrong for judging the last frame, which a loop snatches away a twelfth of a
second after it arrives. With it off, playing stops **on** the last frame, and
pressing play again from there starts the clip over rather than doing nothing.
`playback.loop` in the [config file](configuration.md) says which one you get on
opening.

<kbd>,</kbd> and <kbd>.</kbd> step through frames from the keyboard, where other
sprite editors put them. The arrows are worth more on the canvas, and stepping
through frames is not something you do with your hand on the drawing.

## Onion skin

<kbd>o</kbd> draws the previous frame faded underneath the current one. It is for
seeing **movement**, whether a limb travelled the same distance between frames 2
and 3 as it did between 1 and 2. That is invisible when you look at one frame at
a time and obvious when you look through them.

## It follows the omarchy theme

The window reads the active omarchy theme's colours and Hyprland's corner
rounding, and follows a theme change live: `omarchy theme set <name>` recolours
it without a restart. On a machine that is not running omarchy it opens on its
built-in defaults. The details are in [how it is built](design.md#the-theme).

The desktop theme never touches the **document's** palette. Your art does not
change colour because you changed themes.

## Looking at the window without a screen

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  OMAPIXEL_SHOT=/tmp/studio.png omapixel-studio drawing.json
```

Renders the window to a PNG and exits. The studio was the one part of this
project that could not be inspected without a display, so a layout change had to
be described and taken on trust. This is how the layout above was checked.

`OMAPIXEL_SHOT_SHEET=colour` opens that panel first, and `replace`, `new` and
`export` work too. A popup does not appear in a window grab, so a panel that only exists once
opened cannot otherwise be looked at without a display.

`OMAPIXEL_DEBUG_INPUT=1` logs every wheel and gesture event as it reaches the
window, and again from the handler that acts on it, straight to stderr. Input
that never arrives and input that arrives and is ignored look identical from the
outside; this tells them apart.
