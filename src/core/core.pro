# The model, and nothing else. No QML, no window, no argument parsing.
#
# It is a static library rather than a pile of sources compiled twice, so there
# is one object file behind both front ends and no way for them to drift.
QT       += core gui
QT       -= widgets
CONFIG   += c++17 staticlib
CONFIG   -= app_bundle
TEMPLATE  = lib
TARGET    = omapixelcore

# Where the language catalogues live in a checkout. Installed builds also
# look beside the binary, and the user's own config directory wins over both.
DEFINES += OMAPIXEL_I18N_DIR=\\\"$$PWD/../../i18n\\\"

# And where the annotated default config lives, for `omapixel --default-config`
# and `omapixel config write`. Installed builds look under /usr/share too.
DEFINES += OMAPIXEL_CONFIG_DIR=\\\"$$PWD/../../config\\\"

HEADERS += \
    Strings.h \
    Toml.h \
    Config.h \
    Grid.h \
    Palette.h \
    Document.h \
    Differences.h \
    Codec.h \
    Ops.h \
    Render.h \
    LayerOperations.h \
    Bridge.h \
    Sessions.h \
    Output.h \
    TextSafety.h

SOURCES += \
    Strings.cpp \
    Toml.cpp \
    Config.cpp \
    Grid.cpp \
    Palette.cpp \
    Document.cpp \
    Differences.cpp \
    Codec.cpp \
    Ops.cpp \
    Render.cpp \
    LayerOperations.cpp \
    Bridge.cpp \
    Sessions.cpp \
    Output.cpp \
    TextSafety.cpp
