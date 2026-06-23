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

namespace QtTerminalLogConfig {
inline constexpr bool TraceEnabled = false;
inline constexpr bool VerboseTraceEnabled = false;
inline constexpr bool ViewportTraceEnabled = true;
}

inline bool qtTerminalTraceEnabled()
{
    return QtTerminalLogConfig::TraceEnabled;
}

inline bool qtTerminalVerboseTraceEnabled()
{
    return QtTerminalLogConfig::VerboseTraceEnabled;
}

inline bool qtTerminalViewportTraceEnabled()
{
    return QtTerminalLogConfig::ViewportTraceEnabled;
}

#define QT_TERMINAL_TRACE_STREAM if (!qtTerminalTraceEnabled()) {} else qDebug()
#define QT_TERMINAL_VERBOSE_TRACE_STREAM if (!qtTerminalVerboseTraceEnabled()) {} else qDebug()
#define QT_TERMINAL_VIEWPORT_TRACE_STREAM if (!qtTerminalViewportTraceEnabled()) {} else qDebug()

#endif // TERMINAL_GLOBAL_H
