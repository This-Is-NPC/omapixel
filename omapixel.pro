# omapixel — a pixel art and animation studio.
#
# One core, two front ends. `core` is a static library with the model and no
# opinion about how it is driven; `cli` and `gui` are both thin things on top of
# it. Order matters here: the two front ends link the library, so it builds
# first.
TEMPLATE = subdirs
CONFIG  += ordered

SUBDIRS = \
    src/core \
    src/cli \
    src/gui

core.subdir = src/core
cli.subdir  = src/cli
cli.depends = core
gui.subdir  = src/gui
gui.depends = core
