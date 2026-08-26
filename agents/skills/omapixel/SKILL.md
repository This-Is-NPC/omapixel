---
name: omapixel
description: >
  Use when creating, editing, inspecting, animating, importing, or rendering
  pixel art and sprites with omapixel, omapixel-studio, .omapixel, or Omapixel
  JSON documents. Also use when coordinating an agent's edits with a live
  Omapixel Studio window. Excludes development of the Omapixel source code.
---

# Omapixel Skill

Operate Omapixel pixel-art and animation documents through the command line.
Use this skill for artwork workflows, not for contributing to Omapixel itself.

## Topic Guides

Read the matching guide before making changes:

- [`drawing.md`](drawing.md) - create, inspect, draw, batch, and verify
- [`animation.md`](animation.md) - clips, frames, sprite sheets, and GIFs
- [`layers.md`](layers.md) - stable layer targets, visibility, locks, and flattening
- [`studio.md`](studio.md) - coordinate safely with a live Studio window

## Command Discovery

```bash
omapixel --help
omapixel <command> --help
```

The usual shape is:

```bash
omapixel <command> <file> [sub-command] [flags]
```

`new`, `import`, `where`, `config`, `plugin`, and `skill` have their own shapes.
Document mutations save in place. There is no `--save` flag.

## Required Workflow

1. Confirm that the CLI is available with `omapixel --version`.
2. Before changing an existing document, run `omapixel where <file>`.
3. If a Studio reports `dirty: true`, ask before writing to that document.
4. Inspect with `info`, `layer list`, `text`, or `show` before editing.
5. Use `batch` for generated art or more than one document mutation.
6. Address layers by stable `--layer-id`; state clip, frame, and scope explicitly.
7. Run `check`, then inspect with `text` or `show`, then render the result.

## Safety Rules

- Never hand-edit a compressed `.omapixel` file. Use the CLI or Studio.
- Never infer a CLI target from the Studio's current selection. Read `where`,
  then pass the reported layer, clip, frame, and scope explicitly.
- Never replace dirty Studio work without the user's confirmation.
- Never use `--anyway` for cropping or lossy flattening without confirmation.
- Never bypass hidden or locked layer safeguards merely to make a command pass.
- Keep render, flatten, and import destinations different from their sources.
- Prefer `.omapixel` for compact editable projects and `.json` when readable
  interchange is specifically useful.

## Exit Codes

| Code | Meaning |
|---:|---|
| `0` | The command succeeded. |
| `1` | The command ran but found or refused something in the art. |
| `2` | The command line is malformed and must be corrected. |

`check` and `diff` deliberately return `1` when they find a problem or a
difference. Do not treat that as a CLI crash.

## Standard Loop

```bash
omapixel new art.omapixel --size 32x32
omapixel info art.omapixel
omapixel batch art.omapixel --script draw.batch
omapixel check art.omapixel
omapixel text art.omapixel
omapixel show art.omapixel
omapixel render art.omapixel -o art.png --scale 8
```

Do not claim visual success from a zero exit code alone. Inspect the text,
terminal drawing, or rendered image and compare it with the request.
