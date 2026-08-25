OMAPIXEL_PROJECT_DIR = $$PWD
OMAPIXEL_SOURCE_ROOT = $$clean_path($$PWD/..)
include($$PWD/../qmake/layout.pri)

QT       += core gui quick testlib gui-private
QT       -= widgets
CONFIG   += testcase c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tst_omapixel

# Compiles the core's sources directly rather than linking the library. It is
# what omawrite does, and it keeps `mise run test` independent of whether the
# main build happens to be up to date.
DEFINES += OMAPIXEL_I18N_DIR=\\\"$$PWD/../i18n\\\"
DEFINES += OMAPIXEL_CONFIG_DIR=\\\"$$PWD/../config\\\"
DEFINES += SOURCE_DIR=\\\"$$PWD/..\\\"

INCLUDEPATH += $$PWD/../src/core $$PWD/../src/gui $$PWD/../src/cli

SOURCES += \
    tst_omapixel.cpp \
    ../src/core/Grid.cpp \
    ../src/core/Palette.cpp \
    ../src/core/Document.cpp \
    ../src/core/Differences.cpp \
    ../src/core/Codec.cpp \
    ../src/core/Ops.cpp \
    ../src/core/Render.cpp \
    ../src/core/LayerOperations.cpp \
    ../src/core/Bridge.cpp \
    ../src/core/Sessions.cpp \
    ../src/core/Output.cpp \
    ../src/core/PluginManifest.cpp \
    ../src/core/PluginRegistry.cpp \
    ../src/core/TextSafety.cpp \
    ../src/core/Strings.cpp \
    ../src/core/Toml.cpp \
    ../src/core/Config.cpp \
    ../src/gui/Theme.cpp \
    ../src/gui/DocumentModel.cpp \
    ../src/gui/PaletteModel.cpp \
    ../src/gui/PixelGridItem.cpp \
    ../src/gui/InputLog.cpp \
    ../src/gui/SessionPublisher.cpp \
    ../src/gui/ChangeLog.cpp \
    ../src/cli/Commands.cpp

HEADERS += \
    ../src/core/Grid.h \
    ../src/core/Palette.h \
    ../src/core/Document.h \
    ../src/core/Differences.h \
    ../src/core/Codec.h \
    ../src/core/Ops.h \
    ../src/core/Render.h \
    ../src/core/LayerOperations.h \
    ../src/core/Bridge.h \
    ../src/core/Sessions.h \
    ../src/core/Output.h \
    ../src/core/PluginManifest.h \
    ../src/core/PluginRegistry.h \
    ../src/core/TextSafety.h \
    ../src/core/Strings.h \
    ../src/core/Toml.h \
    ../src/core/Config.h \
    ../src/gui/Theme.h \
    ../src/gui/DocumentModel.h \
    ../src/gui/PaletteModel.h \
    ../src/gui/PixelGridItem.h \
    ../src/gui/InputLog.h \
    ../src/gui/SessionPublisher.h \
    ../src/gui/ChangeLog.h \
    ../src/cli/Commands.h
