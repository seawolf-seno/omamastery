QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = omamastery
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/classbook.h \
    src/cli.h \
    src/samples.h \
    src/school.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/classbook.cpp \
    src/cli.cpp \
    src/samples.cpp \
    src/school.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
