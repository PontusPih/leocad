TEMPLATE = app
TARGET = leocad_tests

CONFIG += console testcase c++17
CONFIG -= app_bundle

QT += concurrent opengl printsupport testlib widgets

greaterThan(QT_MAJOR_VERSION, 5) {
	QT += openglwidgets
}

INCLUDEPATH += ../common

HEADERS += \
	../common/lc_shadowparser.h \
	../common/lc_snapshapes.h \
	lc_tests.h

SOURCES += \
	../common/lc_shadowparser.cpp \
	../common/lc_snapshapes.cpp \
	lc_objecthistory_test.cpp \
	lc_snapshapes_test.cpp \
	main.cpp
