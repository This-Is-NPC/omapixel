# omapixel

> ### *Pixels, palettes... and prompt.*

**omapixel is a pixel art and animation studio your AI agent can drive. Every
operation is on the command line, and there is a window for when you take
over.**

![the last-horizon photograph dissolving into the omarchy wallpaper](examples/last-horizon-omarchy.gif)

No mouse in the loop. No colour baked into a pixel. No screenshotting a running
window to find out what got drawn.

---

## How it works

1. `omapixel new` writes a document, or open one that already exists.
2. Draw: `paint`, `line`, `rect`, `fill`, `edit`, from a prompt, a script or the
   studio.
3. Look: `text` for the letters, `show` for the terminal, `render` for a PNG.
4. Animate with `clip` and `frame`; the studio plays it back, looping or not.
5. Hand over the JSON, a PNG, or a sprite sheet.

Anything generated goes through `batch`: one pass over one document, read once
and written once. Everything an agent does should go through it.

## Quickstart

On Omarchy:

```bash
sudo pacman -S omapixel
```

From a checkout:

```bash
mise run deps      # says what is missing and how to install it
mise run build     # the command line and the studio
mise run install   # into ~/.local/bin
```

Draw something and look at it without leaving the terminal:

```bash
omapixel new heart.json --size 16x16
omapixel rect heart.json --from 4,4 --to 11,9 --slot R --filled
omapixel show heart.json
```

`R` is a palette slot. A pixel stores the letter, the palette stores the colour,
so recolouring every `R` in the document is one edit. See
[the format](docs/format.md).

Then open the same file in the window:

```bash
omapixel-studio heart.json
```

New here? Start with [Getting started](docs/getting-started.md).

## Commands

| Command | What it does |
| :--- | :--- |
| `new` `info` `check` | make one, describe one, say what is wrong with one |
| `show` `text` `render` | look at one: in the terminal, as letters, as a PNG |
| `paint` `line` `rect` `fill` | draw |
| `edit` | `clear` `shift` `flip` `swap` over a whole frame |
| `clip` `frame` | `add` `dup` `rm` `move` `rename` `fps` |
| `palette` | `list` `set` `rm` |
| `resize` | change the frame, keeping the drawing centred |
| `batch` | many commands over one document, read once and written once |
| `diff` | what differs between two documents |
| `import` `export` | read and write somebody else's sprite catalog |
| `config` `i18n` | the settings file, and what a translation is missing |

`--clip` and `--frame` default to the first clip and frame 0, so a document with
one clip needs no flags at all. Exit `1` means the answer was no; exit `2` means
the command was wrong.

> **`batch` is not an optimisation, it is the way to generate art.** Every
> separate invocation starts a Qt application, parses the whole document and
> writes it back. Turning a photograph into a 160×90 picture one command at a
> time took 3621 processes and **4 minutes 26 seconds**; the identical
> document through `batch` takes **0.1 seconds**.

## Looking at what got drawn

An agent that cannot see its own work is guessing, and you end up reviewing every
attempt by hand. Three commands answer "what is actually there?":

```bash
omapixel text   heart.json                    # the raw grid, one letter per pixel
omapixel show   heart.json                    # in the terminal, in colour
omapixel render heart.json -o h.png --scale 12 --sheet
```

`text` is the frame exactly as it is stored, which makes it the thing to diff,
to grep, or to reason about. `show` prints half-block characters in 24-bit
colour, two sprite rows per terminal row, so the drawing keeps its proportions
instead of arriving twice as tall. `render` writes a PNG with no display
attached, and `--sheet` lays every frame of the clip side by side, in one image
that shows whether an animation holds together.

Without these, a complete command set still leaves somebody screenshotting a
running window. With them, the window is optional.

## The studio, for when you take over

```bash
omapixel-studio person.json
```

![the studio](docs/studio.png)

Tools and the ten colours in hand down the left, the drawing in the middle,
panels on the right, the clip and its frames along the bottom, and a bar of live
keys under the canvas.

- **Every control is reachable from the keyboard.** A cursor walks the drawing a
  pixel at a time and paints where it stands; <kbd>Tab</kbd> walks the window;
  <kbd>Esc</kbd> always hands the keyboard back to the drawing.
- **It wears your omarchy theme**, live. `omarchy theme set` recolours it
  without a restart, corner rounding included. Your art never changes colour
  because your desktop did.
- **Its settings and every keybinding are one TOML file** in the shape the rest
  of an omarchy machine uses, watched, so saving it rebinds the keys while the
  window is open.
- **Its words come from a JSON file per language**, so translating is copying
  one file and changing the right-hand sides.

## Learn more

Full docs live in [`docs/`](docs/).

- **[Getting started](docs/getting-started.md)**: draw, animate and export something, start to finish.
- **[The command line](docs/cli.md)**: every command, every flag, with examples.
- **[The studio](docs/studio.md)**: the window, its panels and its keys.
- **[The format](docs/format.md)**: what is in the file, and how to write one by hand.
- **[Settings and keys](docs/configuration.md)**: one TOML file, every setting, every binding.
- **[Adding a language](docs/i18n.md)**: every word comes from a JSON file.
- **[How it is built](docs/design.md)**: one core, two front ends, and why it is shaped this way.
- **[Releasing](docs/releasing.md)**: push a tag, and how the package reaches Omarchy.

## License

omapixel is available under the [MIT License](LICENSE).

---

<sub>Needs Qt 6 and a C++ compiler: `sudo pacman -S --needed qt6-base qt6-declarative gcc make`.</sub>

<sub>Built for [Omarchy](https://omarchy.org) with [Omakiten](https://github.com/This-Is-NPC/omakiten).</sub>
