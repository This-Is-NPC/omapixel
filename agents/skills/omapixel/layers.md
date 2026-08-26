# Layers

Layer order is bottom-to-top and indices are zero-based. IDs survive renames and
reordering, so automation should use `--layer-id` instead of a display name.

## Inspect First

```bash
omapixel layer art.omapixel list
omapixel info art.omapixel
```

Multilayer mutations require an explicit target. Never guess from layer order or
from the Studio's selected layer.

## Structure and Metadata

```bash
omapixel layer art.omapixel add --id hero --name "Hero" --storage animated
omapixel layer art.omapixel rename --layer-id hero --name "Main Hero"
omapixel layer art.omapixel move --layer-id hero --index 1
omapixel layer art.omapixel set --layer-id hero --visible true --opacity 220
omapixel layer art.omapixel dup --layer-id hero \
  --id hero-copy --name "Hero Copy"
```

Locked layers refuse every mutation. Hidden layers refuse content and
destructive stack operations unless `--include-hidden` is explicit. Ask before
bypassing either protection; do not add the flag merely because a command failed.

## Render and Flatten

Visible layers composite bottom-to-top by default:

```bash
omapixel show art.omapixel
omapixel render art.omapixel -o composite.png --scale 8
omapixel render art.omapixel -o hero.png --isolated --layer-id hero --scale 8
```

Flattening always writes a separate document:

```bash
omapixel flatten art.omapixel -o art-flat.omapixel
```

If palette quantization would lose colour fidelity, flatten exits `1` and
reports the consequences. Use `--anyway` only after the user accepts that loss.
Locked layers can never be discarded by flattening.

After structural edits, run `layer list`, `check`, and a composite render.
