# Getting started

By the end of this page you will have drawn a sprite, animated it, watched it
play, and written it out as a PNG, without leaving the terminal, and then again
in the window.

## Install

You need Qt 6 and a C++ compiler. On Arch:

```bash
sudo pacman -S --needed qt6-base qt6-declarative gcc make
```

Then, from the project:

```bash
mise run deps     # checks what is missing and prints the line to install it
mise run build    # builds the command line and the studio
mise run test     # the unit tests, if you want to be sure
```

`mise.toml` is the only entry point; there is no Makefile. If mise refuses the
file, run `mise trust` once.

The two programs land in `build/`:

```
build/bin/omapixel          the command line
build/bin/omapixel-studio   the window
```

`mise run cli -- <args>` and `mise run studio <file>` run them without typing the
paths. The rest of this page writes `omapixel` for the first one.

## A first drawing

Make an empty document. It is 32×24 unless you ask for something else:

```bash
omapixel new heart.json --size 16x16
```

```
heart.json: 16x16, one clip
```

Every document starts with one clip, called `idle`, holding one blank frame, and
a palette of seventeen colours. Look at the palette:

```bash
omapixel palette heart.json list
```

```
I  #1A1B26
C  #2B3048
...
R  #F7768E
```

A pixel is not a colour, it is a **letter**: the left column above. `R` is the
pink. Draw with it:

```bash
omapixel rect  heart.json --from 2,4  --to 6,8  --slot R --filled
omapixel rect  heart.json --from 9,4  --to 13,8 --slot R --filled
omapixel rect  heart.json --from 4,7  --to 11,10 --slot R --filled
omapixel line  heart.json --from 5,11 --to 10,11 --slot R
omapixel line  heart.json --from 6,12 --to 9,12  --slot R
omapixel paint heart.json --at 7,13 --slot R
omapixel paint heart.json --at 8,13 --slot R
```

Look at it. `show` draws in the terminal, in colour:

```bash
omapixel show heart.json
```

If the shape is wrong, `omapixel edit heart.json clear` starts the frame over.

## Animating it

A clip is a list of frames. Add a second one that copies the first:

```bash
omapixel frame heart.json dup
```

Now edit frame 1 without touching frame 0. Shift the whole drawing down a pixel
so the heart looks like it is beating:

```bash
omapixel edit heart.json shift --frame 1 --by 0,1
```

Two frames at eight per second is a fast flutter. Slow it down:

```bash
omapixel clip heart.json fps --name idle --fps 4
```

`info` tells you where you are. `drawn` counts the pixels that are not empty,
added up across the clip's frames. It is the quickest way to notice a frame you
cleared by accident:

```bash
omapixel info heart.json
```

```json
{
  "size": {"w": 16, "h": 16},
  "palette": [...],
  "clips": [{"name": "idle", "fps": 4, "frames": 2, "drawn": 164}]
}
```

## Getting a picture out

One frame, at eight screen pixels per sprite pixel:

```bash
omapixel render heart.json -o heart.png --scale 8
```

Every frame side by side, as a sprite sheet, on a checkerboard so transparency
is visible:

```bash
omapixel render heart.json -o sheet.png --scale 8 --sheet --checker
```

An animated GIF of the complete clip:

```bash
omapixel render heart.json -o heart.gif --format gif --scale 8 --fps 4 --loop
```

You can also start from an existing image. This turns each 8×8 source block into
one logical pixel and creates a normal Omapixel document:

```bash
omapixel import-image photo.png -o photo.json --scale 8
```

## The same file in the window

```bash
mise run studio heart.json
```

The drawing is in the middle. Tools and the ten colours you are drawing with run
down the left; the palette, a true-size preview, the sprite's size and a
reference image are panels on the right; the clip and its frames are along the
bottom. Under the drawing, a bar shows the keys that are live right now.

Press <kbd>Space</kbd> to play the animation, <kbd>b</kbd> to draw,
<kbd>e</kbd> to erase, <kbd>Ctrl</kbd>+<kbd>S</kbd> to save.
[The studio](studio.md) has the rest.

For layered documents, the right-hand list is only the compact layer browser.
Activate a row or press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd> to open or
focus the independent Layer tool. It can be moved or resized onto another
monitor; <kbd>Tab</kbd> reaches its actions, <kbd>Space</kbd> or <kbd>Enter</kbd>
activates one, and <kbd>Esc</kbd> closes it and returns focus to the layer list.
The command line remains explicit: `where` reports the Studio view, but no CLI
command consumes the active row or tool-window focus as a default.

Nothing is special about a file made on the command line. The window opens the
same documents, saves the same format, and applies the same rules. Work in
whichever suits the moment.

## Where to go next

- [The command line](cli.md): every command and flag
- [The studio](studio.md): the window in detail
- [The format](format.md): writing a document by hand, or generating one
- [Settings and keys](configuration.md): one TOML file, window, canvas, every keybinding

Contributors can find the complete test matrix and performance gate in [How it
is built](design.md#tests-and-acceptance).
