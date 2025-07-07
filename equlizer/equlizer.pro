#-------------------------------------------------
#
# Project created by QtCreator 2025-07-02T11:55:15
#
#-------------------------------------------------

QT       += core gui printsupport widgets

CONFIG += c++11


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = equlizer
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    qcustomplot.cpp

HEADERS  += mainwindow.h \
    qcustomplot.h

FORMS    += mainwindow.ui

