QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = omamastery_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/classbook.h ../src/samples.h ../src/school.h ../src/theme.h ../src/translator.h
SOURCES += omamastery_tests.cpp ../src/backend.cpp ../src/classbook.cpp ../src/samples.cpp ../src/school.cpp \
           ../src/theme.cpp ../src/translator.cpp
