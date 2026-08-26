# omapixel

> ### *Pixels, palettes... and prompt.*

**omapixel is a pixel art and animation studio your AI agent can drive. Every
operation is on the command line, and there is a window for when you take
over.**

No mouse in the loop. No colour baked into a pixel. No screenshotting a running
window to find out what got drawn.

## Made with omapixel

<table>
  <tr>
    <td width="62%" align="center">
      <a href="examples/last-horizon-omarchy/last-horizon-omarchy.omapixel">
        <img src="examples/last-horizon-omarchy/last-horizon-omarchy.gif" alt="The Last Horizon photograph dissolving into Omarchy, Omapixel, and back">
      </a>
    </td>
    <td width="38%" align="center">
      <a href="examples/escape-from-omarchy/escape-from-omarchy.omapixel">
        <img src="examples/escape-from-omarchy/escape-from-omarchy.gif" alt="An animated newspaper ending with Oligarchy transforming into Omarchy and a beating Omapixel heart" width="260">
      </a>
    </td>
  </tr>
  <tr>
    <td align="center"><sub><strong>Last Horizon × Omarchy × Omapixel</strong><br>109 frames · 12 fps · three-stage dissolve with a pulsing heart</sub></td>
    <td align="center"><sub><strong>Escape from Omarchy</strong><br>102 frames · 8 fps · layered newspaper animation</sub></td>
  </tr>
</table>

Both previews come from editable `.omapixel` projects. Open one in the Studio,
inspect every layer and frame, or [browse all examples](examples/).

---

## How it works

1. `omapixel new` writes a document, or open one that already exists.
2. Draw: `paint`, `line`, `rect`, `fill`, `edit`, from a prompt, a script or the
   studio.
3. Look: `text` for the letters, `show` for the terminal, `render` for a PNG.
4. Animate with `clip` and `frame`; the studio plays it back, looping or not.
5. Hand over the JSON, compressed `.omapixel`, a PNG, or a sprite sheet.

Anything generated goes through `batch`: one pass over one document, read once
and written once. Everything an agent does should go through it.

## Quickstart

On Omarchy:

```bash
sudo pacman -S omapixel
omapixel skill install # teach supported coding agents how to drive it
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

Use a `.omapixel` suffix when the editable document should be compressed;
`.json` remains the default readable interchange form:

```bash
omapixel new animation.omapixel --size 160x90
omapixel-studio animation.omapixel
```

The Studio's Save As dialog offers both representations and subsequent saves
preserve the selected one.

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
| `show` `text` `render` | look at one: in the terminal, as letters, as PNG or animated GIF |
| `paint` `line` `rect` `fill` | draw |
| `edit` | `clear` `shift` `flip` `swap` over a whole frame |
| `clip` `frame` | `add` `dup` `rm` `move` `rename` `fps` |
| `palette` | `list` `set` `rm` |
| `resize` `trim` | resize around the centre, or remove empty outer borders |
| `batch` | many commands over one document, read once and written once |
| `diff` | what differs between two documents |
| `import` `export` | read and write somebody else's sprite catalog |
| `import-image` | turn PNG, JPEG or WebP into a new document or layer |
| `config` `i18n` | the settings file, and what a translation is missing |
| `skill` | inspect or install the Omapixel skill for coding agents |

`--clip` and `--frame` default to the first clip and frame 0, so a document with
one clip needs no flags at all. Exit `1` means the answer was no; exit `2` means
the command was wrong.

> **Use `batch` for generated art.** It applies many commands after one read and
> commits once, instead of starting a process and rewriting the document for
> every operation.

## Looking at what got drawn

An agent that cannot see its own work is guessing, and you end up reviewing every
attempt by hand. Three commands answer "what is actually there?":

```bash
omapixel text   heart.json                    # the composite, flattened to palette slots
omapixel show   heart.json                    # the composite in the terminal
omapixel render heart.json -o h.png --scale 12 --sheet
omapixel render heart.json -o layer.png --isolated --layer-id hero
omapixel render heart.json -o animation.gif --format gif --fps 12 --loop
```

These surfaces use the visible bottom-to-top composite by default. `--isolated
--layer-id ID` or an exact `--layer NAME` renders one visible layer with its
opacity, without its neighbours. The CLI never consumes Studio selection state.
`text` keeps transparent pixels as `.`, while `show` and PNG retain composed RGBA;
`--checker` is a final background for those visual surfaces and is never written
into a layer. `render --sheet` applies the selected view to every frame side by
side.

Without these, a complete command set still leaves somebody screenshotting a
running window. With them, the window is optional.

Layer stack control is available without opening Studio:

```bash
omapixel layer heart.json list
omapixel layer heart.json rename --layer-id hero --name "Main Hero"
omapixel layer heart.json move --layer-id hero --index 1
```

Multilayer mutations require explicit layer identity. See [the CLI reference](docs/cli.md)
for scope, lock/hidden safety, batch behavior, and `flatten` reports.

The complete cross-surface gate is `mise run check`. It includes offscreen
Studio/CLI fixtures, deterministic v2 properties, i18n coverage and the canonical
performance benchmark. The workload and stop limits are in [How it is
built](docs/design.md#tests-and-acceptance).

## The studio, for when you take over

```bash
omapixel-studio person.json
```

![the studio](docs/studio.png)

Tools and the ten colours in hand down the left, the drawing in the middle,
layers and inspector panels on the right, the clip and its frames along the
bottom, and a bar of live keys under the canvas.

- **Every control is reachable from the keyboard.** A cursor walks the drawing a
  pixel at a time and paints where it stands; <kbd>Tab</kbd> walks the window;
  <kbd>Esc</kbd> always hands the keyboard back to the drawing.
- **It wears your omarchy theme**, live. `omarchy theme set` recolours it
  without a restart, corner rounding included. Your art never changes colour
  because your desktop did.
- **It follows the file you have open**, live. An agent editing through the
  command line lands in the window as it happens, the status line says what
  changed, and if it replaced unsaved work one Ctrl+Z brings your version
  back. `omapixel where` tells that agent which windows hold a document — and
  whether they have unsaved work — before it writes. Even an untitled window
  is backed by a runtime file, so "draw something" works before any save.
- **Its settings and every keybinding are one TOML file** in the shape the rest
  of an omarchy machine uses, watched, so saving it rebinds the keys while the
  window is open.
- **Its words come from a JSON file per language**, so translating is copying
  one file and changing the right-hand sides.

## Learn more

Full docs live in [`docs/`](docs/).

- **[Getting started](docs/getting-started.md)**: draw, animate and export something, start to finish.
- **[Examples](examples/)**: editable projects paired with their rendered GIF previews.
- **[The command line](docs/cli.md)**: every command, every flag, with examples.
- **[The studio](docs/studio.md)**: the window, its panels and its keys.
- **[The format](docs/format.md)**: the schema, composition rules, and layer behavior.
- **[Plugins](docs/plugins.md)**: install, inspect, run, or author an API 1 export plugin.
- **[Settings and keys](docs/configuration.md)**: one TOML file, every setting, every binding.
- **[Adding a language](docs/i18n.md)**: translate the JSON catalogue and check its coverage.
- **[How it is built](docs/design.md)**: one core, two front ends, and why it is shaped this way.

## License

omapixel is available under the [MIT License](LICENSE).

---

<sub>Needs Qt 6 and a C++ compiler: `sudo pacman -S --needed qt6-base qt6-declarative gcc make`.</sub>

<sub>Built for [Omarchy](https://omarchy.org) with [Omakiten](https://github.com/This-Is-NPC/omakiten).</sub>
