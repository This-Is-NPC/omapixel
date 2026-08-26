# Releasing

One action releases omapixel: pushing a tag.

```bash
git tag -a v1.0.0 -m "omapixel 1.0.0"
git push origin v1.0.0
```

Tags are `vMAJOR.MINOR.PATCH`, optionally with a suffix (`v1.1.0-rc1`). Anything
else is ignored, so a `docs-frozen` tag does not ship a release.

## The version lives in one file

`version.pri`, at the root:

```
VERSION = 1.0.0
```

Both binaries read it — the CLI and the studio each answer `--version` with the
same string, because qmake turns that line into a generated `version.h` they
both include. Nothing else in the tree holds a copy.

It is *not* derived from the tag, so the two are moved by hand and can drift.
`mise run check` will not catch that; the release workflow will, and refuses to
publish a tag whose version does not match the tree. Bump `version.pri` in the
commit you tag.

## What the workflow does

Very little, on purpose:

1. Builds and runs the tests (`mise run check`).
2. Checks the tag against `version.pri`.
3. Writes the release notes (`mise run notes`).
4. Creates the GitHub release.

**No binaries are uploaded.** omapixel is distributed through Omarchy's package
repository, which compiles from the source tarball GitHub generates for the tag.
Publishing binaries nobody installs is a thing to keep working for no one.

## The release notes

`mise run notes` builds them from two things, in this order:

- **The body of the tag annotation** — everything after its first line. This is
  where a release says something the commits cannot. `git tag -a v1.0.0` with a
  one-line `-m` leaves it empty, so routine releases cost nothing.
- **The commits since the previous tag**, grouped by their conventional prefix.
  `feat(studio): x` becomes a line under Features; a `!` puts it under Breaking
  changes.

To see what a tag would say before pushing it:

```bash
OMAPIXEL_TAG=v1.0.0 mise run notes
```

GitHub's own generated notes are not used: they list merged pull requests, and
this project has none.

## Getting it to Omarchy users

The release is upstream of the package, not the package itself. Omarchy ships
from `omacom-io/omarchy-pkgs`, a pacman repository its maintainers build and
sign — users have `[omarchy]` in `pacman.conf` already.

A package there is one directory:

```
pkgbuilds/omapixel/
├── PKGBUILD                 # compiles from the GitHub tarball of the tag
└── .omarchy/package.json    # {"source": "local"}
```

Adding it, and every version bump after, is a pull request they accept or
decline. `pkgver` and `sha256sums` are updated there — nothing in this repo
pushes to it.

Being in that repository makes omapixel *available* (`sudo pacman -S omapixel`).
Shipping on new Omarchy installs is a separate list in a separate repository:
`install/omarchy-base.packages` in `basecamp/omarchy`.

## When something goes wrong

**Before the release exists** (test failure, version mismatch): delete the tag,
fix, tag again.

```bash
git push --delete origin v1.0.0
git tag -d v1.0.0
```

A tag someone may already have pulled gets a new version instead — the source
tarball hangs off that tag, and a package may already pin its checksum.

## Trying it locally

```bash
mise run check                      # what the workflow runs
mise run dist                       # the install tree, staged under dist/
OMAPIXEL_TAG=v1.0.0 mise run notes  # the release body
```
