#-------------------------------------------------
#
# Project created by QtCreator 2025-07-02T11:55:15
#
#-------------------------------------------------

QT       += core gui printsupport widgets

CONFIG += c++11

INCLUDEPATH += /home/user/work/alsa/install/include
LIBS += -L/home/user/work/alsa/install/lib -lasound

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = equlizer
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    qcustomplot.cpp \
    audiocapture.cpp

HEADERS  += mainwindow.h \
    qcustomplot.h \
    audiocapture.h

FORMS    += mainwindow.ui

