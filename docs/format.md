# The format

Omapixel uses the strict clean-cut format v2. The complete executable contract,
including the schema, layer storage rules, path diagnostics, deterministic
ordering, and migration inventory, is documented in [format-v2.md](format-v2.md).

The production codec accepts only documents with `version: 2`. A document has
an ordered palette, stable-ID clips containing timing and frame counts, and an
ordered clip-wide layer stack. Raster data belongs to layer cels: one `shared`
cel for all frames or one `animated` cel for every `(clip, frame)` pair.

The machine-readable schema is [format-v2.schema.json](format-v2.schema.json).
Canonical valid and invalid documents are under `tests/fixtures/format-v2/` and
the executable contract gate is:

```bash
mise run format-v2
```

There is no runtime v1 compatibility path. Existing tracked examples and
fixtures are migrated atomically as part of the v2 model/codec change.
