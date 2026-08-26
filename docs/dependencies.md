# Dependencies

The dependencies differ depending on whether omapixel is being installed as an
Omarchy package, built from a checkout, or tested through the complete gate.

## Installing the Omarchy package

An Omarchy installation needs no manual dependency setup:

```bash
sudo pacman -S omapixel
```

The package metadata declares the runtime requirements, and `pacman` resolves
them. Do not copy a build-dependency command into an Omarchy installation.

The current Omarchy base package list also matters when describing what is
already present on an Omarchy machine. It directly includes:

- `qt6-imageformats`
- `hyprland`
- `wtype`
- `xdg-desktop-portal-gtk`
- `xdg-desktop-portal-hyprland`

`qt6-base`, `qt6-declarative`, `zstd`, and `hicolor-icon-theme` arrive
transitively rather than being direct entries in that base list. This describes
the current package composition, not pinned package versions. The source of
this information is Omarchy's `install/omarchy-base.packages`.

## Building from a checkout

The checkout build uses the system Qt and C++ toolchain. `mise` is the task
runner; `mise run deps` checks the commands and Qt headers that the build uses.
The required tool and library roles are:

- `qmake6`, from `qt6-base`
- Qt Core, Gui, Test, and Quick headers, from `qt6-base` and `qt6-declarative`
- `g++`, from `gcc`
- `make`
- `zstd`

Install `mise` separately if it is not already available, then let the project
check the checkout:

```bash
mise run deps
mise run build
```

On Arch, `mise run deps` prints the appropriate `pacman` command for missing
checkout build dependencies. It does not install them automatically.

## Optional WebP support

`qt6-imageformats` supplies the Qt image plugin used for WebP import. A normal
build and the ordinary unit tests can run without WebP support, but WebP input
needs that plugin at runtime. It is present in the current Omarchy base package
list and is included by the packaged application for that reason.

## Ordinary tests

Run the C++/Qt unit tests with:

```bash
mise run test
```

These tests use Qt's offscreen platform and do not need a display, Wayland
compositor, Hyprland, Sway, or `wtype`. Python is not needed for this focused
test task.

## Full headless and Wayland checks

The complete CI gate is:

```bash
mise run check
```

In addition to the ordinary tests and build, it runs Python contracts, image
contracts, offscreen Studio checks, and the keyboard journey through a nested
Wayland session. A clean CI/container environment therefore needs:

- `python`
- `qt6-imageformats`
- `sway`
- `wtype`
- `mesa`

The Wayland test starts Sway with its headless Pixman backend and uses `wtype`
to exercise keyboard input. Mesa provides Qt's software rendering path. These
packages are test dependencies, not a replacement for the runtime dependency
resolution performed by `pacman`.
