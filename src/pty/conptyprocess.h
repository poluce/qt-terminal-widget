#ifndef CONPTYPROCESS_H
#define CONPTYPROCESS_H

#include "iptyprocess.h"
#include <windows.h>
#include <vector>
#include <QLibrary>
#include <QMutex>
#include <QTimer>
#include <QThread>

// RS5 Windows SDK Pseudo Console definitions in case the targeting Windows SDK is older
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE \
  ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)

typedef VOID* HPCON;
#endif

template <typename T>
std::vector<T> vectorFromString(const std::basic_string<T> &str)
{
    return std::vector<T>(str.begin(), str.end());
}

class WindowsContext
{
public:
    typedef HRESULT (*CreatePseudoConsolePtr)(
            COORD size,         // ConPty Dimensions
            HANDLE hInput,      // ConPty Input
            HANDLE hOutput,     // ConPty Output
            DWORD dwFlags,      // ConPty Flags
            HPCON* phPC);       // ConPty Reference

    typedef HRESULT (*ResizePseudoConsolePtr)(HPCON hPC, COORD size);
    typedef VOID (*ClosePseudoConsolePtr)(HPCON hPC);

    WindowsContext()
        : createPseudoConsole(nullptr)
        , resizePseudoConsole(nullptr)
        , closePseudoConsole(nullptr)
    {
    }

    bool init()
    {
        if (createPseudoConsole)
            return true;

        HMODULE kernel32Handle = GetModuleHandleW(L"kernel32.dll");
        if (kernel32Handle == nullptr)
        {
            kernel32Handle = LoadLibraryW(L"kernel32.dll");
        }

        if (kernel32Handle != nullptr)
        {
            createPseudoConsole = (CreatePseudoConsolePtr)GetProcAddress(kernel32Handle, "CreatePseudoConsole");
            resizePseudoConsole = (ResizePseudoConsolePtr)GetProcAddress(kernel32Handle, "ResizePseudoConsole");
            closePseudoConsole = (ClosePseudoConsolePtr)GetProcAddress(kernel32Handle, "ClosePseudoConsole");
            if (createPseudoConsole == NULL || resizePseudoConsole == NULL || closePseudoConsole == NULL)
            {
                m_lastError = QString("WindowsContext/ConPty error: PseudoConsole APIs not found in kernel32.dll");
                return false;
            }
        }
        else
        {
            m_lastError = QString("WindowsContext/ConPty error: Unable to load kernel32.dll");
            return false;
        }

        return true;
    }

    QString lastError()
    {
        return m_lastError;
    }

public:
    CreatePseudoConsolePtr createPseudoConsole;
    ResizePseudoConsolePtr resizePseudoConsole;
    ClosePseudoConsolePtr closePseudoConsole;

private:
    QString m_lastError;
};

class PtyBuffer : public QIODevice
{
    friend class ConPtyProcess;
    Q_OBJECT
public:
    PtyBuffer() {  }
    ~PtyBuffer() { }

    qint64 readData(char *data, qint64 maxlen) override { Q_UNUSED(data); Q_UNUSED(maxlen); return 0; }
    qint64 writeData(const char *data, qint64 len) override { Q_UNUSED(data); Q_UNUSED(len); return 0; }

    bool   isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_readBuffer.size(); }
    qint64 size() const override { return m_readBuffer.size(); }

    void emitReadyRead()
    {
        QTimer::singleShot(1, this, [this]()
        {
             emit readyRead();
        });
    }

private:
    QByteArray m_readBuffer;
};

class ConPtyProcess : public IPtyProcess
{
public:
    ConPtyProcess();
    ~ConPtyProcess() override;

    bool startProcess(const QString &shellPath, QStringList environment, qint16 cols, qint16 rows) override;
    bool resize(qint16 cols, qint16 rows) override;
    bool kill() override;
    PtyType type() override;
    QString dumpDebugInfo() override;
    QIODevice *notifier() override;
    QByteArray readAll() override;
    qint64 write(const QByteArray &byteArray) override;
    bool isAvailable() override;
    void moveToThread(QThread *targetThread) override;

private:
    HRESULT createPseudoConsoleAndPipes(HPCON* phPC, HANDLE* phPipeIn, HANDLE* phPipeOut, HANDLE* phPipePTYIn, HANDLE* phPipePTYOut, qint16 cols, qint16 rows);

private:
    WindowsContext m_winContext;
    HPCON m_ptyHandler;
    HANDLE m_hPipeIn, m_hPipeOut;

    QThread *m_readThread;
    QMutex m_bufferMutex;
    PtyBuffer m_buffer;
};

#endif // CONPTYPROCESS_H
