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

INCLUDEPATH += $$PWD/../core
HEADERS     += Commands.h
SOURCES     += main.cpp Commands.cpp

# Links the same object file the studio does. That is the whole reason this is
# one C++ project and not two programs: `resize` used to exist once in QML and
# once in Python, with a comment in each pointing at the other.
LIBS        += -L$$OUT_PWD/../core -lomapixelcore
PRE_TARGETDEPS += $$OUT_PWD/../core/libomapixelcore.a
