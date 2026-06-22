#ifndef TERMINAL_GLOBAL_H
#define TERMINAL_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(QT_TERMINAL_WIDGET_SHARED)
#  if defined(QT_TERMINAL_WIDGET_LIBRARY)
#    define QT_TERMINAL_WIDGET_EXPORT Q_DECL_EXPORT
#  else
#    define QT_TERMINAL_WIDGET_EXPORT Q_DECL_IMPORT
#  endif
#else
#  define QT_TERMINAL_WIDGET_EXPORT
#endif

#endif // TERMINAL_GLOBAL_H
