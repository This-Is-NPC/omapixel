QT       += core gui qml quick
CONFIG   += c++17
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = omapixel-studio

INCLUDEPATH += $$PWD/../core

HEADERS += DocumentModel.h PixelGridItem.h Theme.h InputLog.h
SOURCES += main.cpp DocumentModel.cpp PixelGridItem.cpp Theme.cpp InputLog.cpp
RESOURCES += resources.qrc

# The same static library the CLI links. One model, two front ends -- which is
# the entire reason this project is C++ rather than QML plus a script.
LIBS           += -L$$OUT_PWD/../core -lomapixelcore
PRE_TARGETDEPS += $$OUT_PWD/../core/libomapixelcore.a
