OMAPIXEL_PROJECT_DIR = $$PWD
OMAPIXEL_SOURCE_ROOT = $$clean_path($$PWD/../..)
include($$PWD/../../qmake/layout.pri)

QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = layer_benchmark

SOURCES += layer_benchmark.cpp
