QT       += core gui quick testlib gui-private
QT       -= widgets
CONFIG   += testcase c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tst_omapixel

# Compiles the core's sources directly rather than linking the library. It is
# what omawrite does, and it keeps `mise run test` independent of whether the
# main build happens to be up to date.
DEFINES += SOURCE_DIR=\\\"$$PWD/..\\\"

INCLUDEPATH += $$PWD/../src/core $$PWD/../src/gui $$PWD/../src/cli

SOURCES += \
    tst_omapixel.cpp \
    ../src/core/Grid.cpp \
    ../src/core/Palette.cpp \
    ../src/core/Document.cpp \
    ../src/core/Codec.cpp \
    ../src/core/Ops.cpp \
    ../src/core/Render.cpp \
    ../src/core/Bridge.cpp \
    ../src/gui/Theme.cpp \
    ../src/gui/DocumentModel.cpp \
    ../src/gui/PixelGridItem.cpp \
    ../src/gui/InputLog.cpp \
    ../src/cli/Commands.cpp

HEADERS += \
    ../src/core/Grid.h \
    ../src/core/Palette.h \
    ../src/core/Document.h \
    ../src/core/Codec.h \
    ../src/core/Ops.h \
    ../src/core/Render.h \
    ../src/core/Bridge.h \
    ../src/gui/Theme.h \
    ../src/gui/DocumentModel.h \
    ../src/gui/PixelGridItem.h \
    ../src/gui/InputLog.h \
    ../src/cli/Commands.h
