OMAPIXEL_PROJECT_DIR = $$PWD
OMAPIXEL_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core gui
QT       -= widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = omapixel

# One place for the finished binaries, whatever the build tree looks
# like. qmake's subdirs mirror the source layout, so without this the
# programs land in build/src/cli and build/src/gui, and every task and
# every line of documentation has to know that.
DESTDIR = $$OUT_PWD/../../bin

# Where the version comes from. One file, both binaries.
include($$PWD/../../version.pri)

# A shadow build can live anywhere, so checkout binaries need one source-tree
# fallback. Installed data beside the executable still wins at runtime.
DEFINES += OMAPIXEL_SKILL_DIR=\\\"$$PWD/../../agents/skills/omapixel\\\"

INCLUDEPATH += $$PWD/../core
HEADERS     += Commands.h PluginCommands.h
SOURCES     += main.cpp Commands.cpp PluginCommands.cpp

# Links the same object file the studio does. That is the whole reason this is
# one C++ project and not two programs: `resize` used to exist once in QML and
# once in Python, with a comment in each pointing at the other.
LIBS        += -L$$OUT_PWD/../core -lomapixelcore -lzstd
PRE_TARGETDEPS += $$OUT_PWD/../core/libomapixelcore.a
