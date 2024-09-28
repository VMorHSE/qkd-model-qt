#-------------------------------------------------
#
# Project created by QtCreator 2014-09-18T14:09:50
#
#-------------------------------------------------

QT       -= gui
QT       += network

TARGET = QKDlib
TEMPLATE = lib

DEFINES += QKDLIB_LIBRARY

INCLUDEPATH += ../include

CONFIG(release, debug|release) {
    DESTDIR += ../release
} else {
    DESTDIR += ../debug
}

SOURCES += qkdunit.cpp \
    GaloisField.cpp \
    GaloisFieldElement.cpp \
    GaloisFieldPolynomial.cpp \
    qkdalice.cpp \
    qkdbob.cpp

HEADERS += ../include/qkdunit.h\
           ../include/GaloisComputer.hpp \
           ../include/GaloisField.h \
           ../include/GaloisFieldElement.h \
           ../include/GaloisFieldPolynomial.h \
           ../include/LDPCCorrect.hpp \
           ../include/qkdlib_global.h \
           ../include/qkdalice.h \
           ../include/qkdbob.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}
