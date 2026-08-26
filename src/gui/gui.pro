OMAPIXEL_PROJECT_DIR = $$PWD
OMAPIXEL_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core gui qml quick
CONFIG   += c++17
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = omapixel-studio

# One place for the finished binaries, whatever the build tree looks
# like. qmake's subdirs mirror the source layout, so without this the
# programs land in build/src/cli and build/src/gui, and every task and
# every line of documentation has to know that.
DESTDIR = $$OUT_PWD/../../bin

# Where the language catalogues live in a checkout. Installed builds also
# look beside the binary, and the user's own config directory wins over both.
# Where the version comes from. One file, both binaries.
include($$PWD/../../version.pri)

INCLUDEPATH += $$PWD/../core

HEADERS += DocumentModel.h PaletteModel.h PixelGridItem.h Theme.h InputLog.h SessionPublisher.h ChangeLog.h
SOURCES += main.cpp DocumentModel.cpp PaletteModel.cpp PixelGridItem.cpp Theme.cpp InputLog.cpp SessionPublisher.cpp ChangeLog.cpp
RESOURCES += resources.qrc

# The same static library the CLI links. One model, two front ends -- which is
# the entire reason this project is C++ rather than QML plus a script.
LIBS           += -L$$OUT_PWD/../core -lomapixelcore -lzstd
PRE_TARGETDEPS += $$OUT_PWD/../core/libomapixelcore.a
