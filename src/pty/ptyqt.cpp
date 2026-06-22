#include "ptyqt.h"

#ifdef Q_OS_WIN
#include "conptyprocess.h"
#endif

#ifdef Q_OS_UNIX
#include "unixptyprocess.h"
#endif

IPtyProcess *PtyQt::createPtyProcess(IPtyProcess::PtyType ptyType)
{
    switch (ptyType)
    {
#ifdef Q_OS_WIN
    case IPtyProcess::ConPty:
        return new ConPtyProcess();
#endif
#ifdef Q_OS_UNIX
    case IPtyProcess::UnixPty:
        return new UnixPtyProcess();
#endif
    case IPtyProcess::AutoPty:
    default:
        break;
    }

#ifdef Q_OS_WIN
    return new ConPtyProcess();
#else
    return new UnixPtyProcess();
#endif
}
