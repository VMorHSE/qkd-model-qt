#ifndef QKDLIB_GLOBAL_H
#define QKDLIB_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(QKDLIB_LIBRARY)
#  define QKDLIBSHARED_EXPORT Q_DECL_EXPORT
#else
#  define QKDLIBSHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // QKDLIB_GLOBAL_H
