QT       += core gui
QT       -= widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = omapixel

INCLUDEPATH += $$PWD/../core
HEADERS     += Commands.h
SOURCES     += main.cpp Commands.cpp

# Links the same object file the studio does. That is the whole reason this is
# one C++ project and not two programs: `resize` used to exist once in QML and
# once in Python, with a comment in each pointing at the other.
LIBS        += -L$$OUT_PWD/../core -lomapixelcore
PRE_TARGETDEPS += $$OUT_PWD/../core/libomapixelcore.a
