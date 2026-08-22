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

HEADERS += \
    Grid.h \
    Palette.h \
    Document.h \
    Codec.h \
    Ops.h \
    Render.h \
    Bridge.h

SOURCES += \
    Grid.cpp \
    Palette.cpp \
    Document.cpp \
    Codec.cpp \
    Ops.cpp \
    Render.cpp \
    Bridge.cpp
