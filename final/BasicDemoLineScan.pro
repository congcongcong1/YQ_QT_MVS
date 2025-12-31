#-------------------------------------------------
#
# Project created by QtCreator 2022-12-15T17:38:35
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TRANSLATIONS = BasicDemoLineScan_zh_EN.ts

TARGET = BasicDemoLineScan
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    MvCamera.cpp

HEADERS  += mainwindow.h \
    MvCamera.h

FORMS    += mainwindow.ui

INCLUDEPATH += $$PWD/../../../../../../include/

LIBS +=  -L$$PWD/../../../../../../lib/64/ -lMvCameraControl


CONFIG += c++11

unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
}

RESOURCES += \
    resource.qrc
