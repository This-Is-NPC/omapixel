# The one place the version is written.
#
# C++ has no manifest -- no Cargo.toml, no package.json -- so the build system
# is the manifest. In qmake that is a .pri: a piece of a project file the real
# ones include. Both front ends read the version from here, so there is no
# second copy to forget when it changes.
VERSION = 1.0.0

# With the quotes already in it: QMAKE_SUBSTITUTES runs the template through
# qmake's own parser, which eats a bare pair of quotes and leaves 1.0.0 as a
# number the compiler chokes on.
VERSION_QUOTED = \"$$VERSION\"

# Handed to C++ as a generated header rather than as a DEFINES flag, and that
# is not a style choice. make compares timestamps, and an object file does not
# depend on the Makefile -- so changing a -D value regenerates the Makefile,
# rebuilds nothing, and leaves a binary quietly reporting the old version. A
# header is a file main.cpp includes, so bumping VERSION recompiles what read
# it. Tested by bumping this line and watching the binaries follow.
version_header.input  = $$PWD/version.h.in
version_header.output = $$OUT_PWD/version.h
QMAKE_SUBSTITUTES += version_header
INCLUDEPATH += $$OUT_PWD
