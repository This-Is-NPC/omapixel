# Drawing

## Create and Inspect

```bash
omapixel new sprite.omapixel --size 32x32
omapixel info sprite.omapixel
omapixel palette sprite.omapixel list
omapixel text sprite.omapixel
omapixel show sprite.omapixel --checker
```

Coordinates are zero-based `X,Y`, with `0,0` at the top left. Sizes are
`COLUMNSxROWS`, such as `32x24`. Pixels store palette slots, not colours.

## Prefer Batch

Generated art should use one transactional batch instead of repeatedly loading
and rewriting the document:

```text
# draw.batch
palette set --slot R --colour "#F7768E"
rect --from 4,4 --to 11,9 --slot R --filled
line --from 5,10 --to 10,10 --slot R
```

```bash
omapixel batch sprite.omapixel --script draw.batch
```

Batch lines omit the document path. Blank lines and `#` comments are allowed.
The batch is all or nothing: one failed line leaves the document unchanged.

Use `paint`, `line`, `rect`, `fill`, `edit`, `palette`, `clip`, `frame`, and
`layer` inside a batch. Keep `new`, `render`, `flatten`, `diff`, imports, and
exports outside because they address another file.

## Primitive Edits

```bash
omapixel paint sprite.omapixel --at 7,13 --slot R
omapixel line sprite.omapixel --from 5,11 --to 10,11 --slot R
omapixel rect sprite.omapixel --from 2,4 --to 6,8 --slot R --filled
omapixel fill sprite.omapixel --at 7,7 --slot B
omapixel edit sprite.omapixel flip --axis x
omapixel edit sprite.omapixel shift --by 0,1
omapixel edit sprite.omapixel swap --slot R --to B
```

Omitting `--slot` erases with `.`. Use palette edits to recolour every pixel
that references a slot rather than repainting those pixels.

## Verify

```bash
omapixel check sprite.omapixel
omapixel text sprite.omapixel
omapixel show sprite.omapixel
omapixel render sprite.omapixel -o sprite.png --scale 8
```

For consequential edits, copy the original first and use `omapixel diff` to
verify exactly what changed. A `diff` exit code of `1` means differences exist.
