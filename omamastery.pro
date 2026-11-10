QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = omamastery
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/classbook.h \
    src/cli.h \
    src/manageclasses.h \
    src/documents.h \
    src/connect/connect.h \
    src/connect/http.h \
    src/connect/importer.h \
    src/setupcli.h \
    src/setupflow.h \
    src/samples.h \
    src/school.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/classbook.cpp \
    src/cli.cpp \
    src/manageclasses.cpp \
    src/documents.cpp \
    src/connect/connect.cpp \
    src/connect/http.cpp \
    src/connect/importer.cpp \
    src/setupcli.cpp \
    src/setupflow.cpp \
    src/samples.cpp \
    src/school.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
