# Settings and keys

Everything the studio decides for you lives in one file, and there is one way to
disagree with it:

```bash
omapixel config write     # ~/.config/omapixel/config.toml, annotated
$EDITOR ~/.config/omapixel/config.toml
```

Saving the file is enough. The studio re-reads it and rebinds itself while it is
open: the menus, the hint bar along the bottom of the canvas, and the keys
themselves all change without a restart.

With no file at all omapixel runs on the defaults. The file is how you disagree,
not how you start; every line in the one `config write` gives you is already
commented out at the value the program uses.

## Where it looks

| | |
|---|---|
| `$OMAPIXEL_CONFIG_PATH` | if set, exactly this file and nowhere else |
| `~/.config/omapixel/config.toml` | otherwise |

`omapixel config` says which of those it used, and what in it differs from the
defaults.

## The settings

```toml
language = ""          # a catalogue name from i18n/, "" follows the system

[window]
width  = 1280
height = 820
hints  = true          # the bar of keys under the drawing

[canvas]
zoom     = "fit"       # "fit", or screen pixels per drawing pixel
grid     = true
onion    = false
big_step = 8           # how far shift and an arrow jumps
caret_margin_x = 0      # edge warning in drawing pixels, or "center"
caret_margin_y = 0

[playback]
loop = true            # ▶ starts the clip over at the end; off stops on it

[studio]
scratch = true         # back untitled windows with a runtime file the CLI can address

[document]
width  = 32            # what File ▸ New starts with
height = 24
fps    = 8

[history]
depth = 80             # undo steps; each one is a whole document

[warnings]
file_mib          = 16
clips             = 256
frames_per_clip   = 1024
frames_total      = 4096
palette_slots     = 256
render_megapixels = 64
```

The `[warnings]` values are advisory, not quotas. Crossing one prints or shows a
warning and the operation continues. Set a value to `0` to disable that warning.
Only arithmetic overflow, impossible image dimensions and actual allocation
failure stop an operation.

`studio.scratch` gives every untitled window a backing file under the runtime
directory, so `omapixel where` can find it and an agent can draw into it live.
It is an address, not a save: the file lives on tmpfs and dies with the
session, the window keeps saying unsaved, and closing still asks. Off, an
untitled window is invisible to the command line. Read when the studio starts.

Numeric settings are checked by domain: document dimensions are `1..2048`
(the format's own ceiling), FPS is `1..60`, zoom is `fit` or `1..40`, and
window dimensions, big step and undo depth must be positive integers.
`canvas.caret_margin_x` and `canvas.caret_margin_y` accept zero or a positive
integer measured in drawing pixels, or `center` to keep the keyboard cursor
centred on that axis after every move. A numeric axis only recentres when the
cursor reaches its margin, without moving the other axis.

`OMAPIXEL_LANG` in the environment beats `language` in the file, so one run can
be in another language without editing anything.

## The keys

An action on the left, the keys that fire it on the right.

```toml
[keys]
save = "ctrl+w"                 # one key
paint = ["enter", "x", "z"]     # several
roulette = ""                   # none: the key goes back to nothing
```

Names are lowercase and joined with `+`: `ctrl`, `shift`, `alt`, `super`, then
the key. Letters and digits are themselves. The rest have names: `enter`,
`esc`, `tab`, `space`, `backspace`, `delete`, `left`, `right`, `up`, `down`,
`home`, `end`, `pageup`, `pagedown`, `f1`–`f12`, `plus`, `minus`, `equal`,
`comma`, `period`, `semicolon`, `slash`, `backslash`, `apostrophe`, `backtick`,
`bracketleft`, `bracketright`. Punctuation may also be written as itself, so
`"["` and `"bracketleft"` are the same binding.

Every action, and what it comes bound to:

| | |
|---|---|
| `new` `open` `save` `save_as` | `ctrl+n` `ctrl+o` `ctrl+s` `ctrl+shift+s` |
| `export_png` `export_sheet` `quit` | `ctrl+e` `ctrl+shift+e` `ctrl+q` |
| `undo` `redo` `clear_frame` `trim` | `ctrl+z` `ctrl+shift+z` `ctrl+delete` `ctrl+shift+t` |
| `tool_pencil` `tool_eraser` `tool_bucket` `tool_picker` `tool_hand` | `b` `e` `f` `i` `h` |
| `caret_left` `caret_right` `caret_up` `caret_down` | the arrows |
| `select_left` `select_right` `select_up` `select_down` | the arrows with shift, extending the selection |
| `select_left_far` … | the arrows with ctrl+shift, extending by `canvas.big_step` |
| `caret_left_far` … | the arrows with ctrl, jumping `canvas.big_step` |
| `paint` `erase` `cancel` | `enter` or `x`, `backspace` or `delete`, `esc` |
| `slot_leader` | `;`, after which the next key names a palette letter |
| `choose_colour` `replace_colour` `roulette` | `c` `shift+c` `r` |
| `draw_mode` `pick_mode` `line_point` | `d` `p` `l` |
| `play` `frame_previous` `frame_next` | `space` `,` `.` |
| `frame_add` `frame_duplicate` | `ctrl+shift+n` `ctrl+d` |
| `frame_move_back` `frame_move_on` | `shift+,` `shift+.` |
| `clip_previous` `clip_next` | `[` `]` |
| `zoom_in` `zoom_out` `zoom_fit` | `ctrl++` or `+`, `ctrl+-` or `-`, `ctrl+0` |
| `toggle_grid` `toggle_onion` `toggle_hints` `toggle_loop` | `m` `o`, both unbound |
| `menus` | `f10`, the keyboard onto the menu bar |

Two things are deliberately not rebindable. **The digits 1 to 0** are the ten
colours in hand: a row of slots rather than ten separate commands. And **the
letters after `slot_leader`** are palette letters, which is the whole point of a
leader key: for one press, the alphabet stops being commands.

## When it is wrong

```bash
omapixel config check
```

```
~/.config/omapixel/config.toml:
  Backspace is on both tool_eraser and erase — one of them wins
  line 12: canvas.grid — wants true or false
  line 19: keys.tool_hnad — no such action
  line 23: window.wibble — nothing reads this
```

It exits non-zero when it finds something, so it fits in whatever you run before
you trust a machine. A misspelled setting does nothing and says nothing on its
own, which is the failure a config file actually has; this is the thing that
says it out loud.

`omapixel --default-config` prints the annotated defaults to standard output,
the way omarchy's own programs do, so a menu or a script can read the whole list
of actions without omapixel being installed anywhere in particular.
