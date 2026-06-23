#include "conptyprocess.h"
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <sstream>
#include <QTimer>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QSysInfo>
#include <cstring>
#include <QDir>

HRESULT ConPtyProcess::createPseudoConsoleAndPipes(HPCON* phPC, HANDLE* phPipeIn, HANDLE* phPipeOut, HANDLE* phPipePTYIn, HANDLE* phPipePTYOut, qint16 cols, qint16 rows)
{
    HRESULT hr{ E_UNEXPECTED };

    // Create the pipes to which the ConPTY will connect
    if (CreatePipe(phPipePTYIn, phPipeOut, NULL, 0) &&
        CreatePipe(phPipeIn, phPipePTYOut, NULL, 0))
    {
        // Mark the PTY-end of the pipes as inheritable. 
        // This is a strict requirement for ConPTY; otherwise CreateProcess fails with 0xc0000142.
        SetHandleInformation(*phPipePTYIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(*phPipePTYOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        // Create the Pseudo Console of the required size, attached to the PTY-end of the pipes
        hr = m_winContext.createPseudoConsole({cols, rows}, *phPipePTYIn, *phPipePTYOut, 0, phPC);
    }

    return hr;
}

ConPtyProcess::ConPtyProcess()
    : IPtyProcess()
    , m_ptyHandler { INVALID_HANDLE_VALUE }
    , m_hPipeIn { INVALID_HANDLE_VALUE }
    , m_hPipeOut { INVALID_HANDLE_VALUE }
    , m_readThread(nullptr)
{
}

ConPtyProcess::~ConPtyProcess()
{
    kill();
}

bool ConPtyProcess::startProcess(const QString &shellPath, QStringList environment, qint16 cols, qint16 rows)
{
    if (!isAvailable())
    {
        m_lastError = m_winContext.lastError();
        if (m_lastError.isEmpty()) {
            m_lastError = "ConPTY is not available on this Windows version.";
        }
        return false;
    }

    if (m_ptyHandler != INVALID_HANDLE_VALUE)
        return false;



    QFileInfo fi(shellPath);
    if (fi.isRelative() || !QFile::exists(shellPath))
    {
        m_lastError = QString("ConPty Error: shell file path must be absolute");
        return false;
    }

    m_shellPath = shellPath;
    m_size = QPair<qint16, qint16>(cols, rows);

    // Prepare environment block (Unicode UTF-16 style)
    std::vector<wchar_t> envV;
    for (const QString &line : environment)
    {
        std::wstring wline = line.toStdWString();
        envV.insert(envV.end(), wline.begin(), wline.end());
        envV.push_back(L'\0');
    }
    if (!envV.empty()) {
        envV.push_back(L'\0');
    }
    LPVOID envArg = envV.empty() ? nullptr : envV.data();

    // Command line arguments (mutable copy needed for CreateProcessW)
    std::wstring shellPathStd = m_shellPath.toStdWString();
    std::vector<wchar_t> cmdArg(shellPathStd.begin(), shellPathStd.end());
    cmdArg.push_back(L'\0');

    HRESULT hr{ E_UNEXPECTED };
    HANDLE hPipePTYIn{ INVALID_HANDLE_VALUE };
    HANDLE hPipePTYOut{ INVALID_HANDLE_VALUE };

    // Create the Pseudo Console and pipes to it
    hr = createPseudoConsoleAndPipes(&m_ptyHandler, &m_hPipeIn, &m_hPipeOut, &hPipePTYIn, &hPipePTYOut, cols, rows);
    if (S_OK != hr)
    {
        m_lastError = QString("ConPty Error: CreatePseudoConsoleAndPipes fail");
        return false;
    }

    // Initialize the necessary startup info struct with 1 slot
    STARTUPINFOEX startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEX);
    startupInfo.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;

    SIZE_T attrListSize{};
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);

    startupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(attrListSize));
    if (!startupInfo.lpAttributeList ||
        !InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attrListSize))
    {
        m_lastError = QString("ConPty Error: InitializeProcThreadAttributeList fail");
        if (hPipePTYIn != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYIn);
        if (hPipePTYOut != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYOut);
        if (INVALID_HANDLE_VALUE != m_hPipeOut) CloseHandle(m_hPipeOut);
        if (INVALID_HANDLE_VALUE != m_hPipeIn) CloseHandle(m_hPipeIn);
        m_ptyHandler = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set Pseudo Console attribute
    hr = UpdateProcThreadAttribute(
                startupInfo.lpAttributeList,
                0,
                PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                m_ptyHandler,
                sizeof(HPCON),
                NULL,
                NULL)
            ? S_OK
            : HRESULT_FROM_WIN32(GetLastError());

    if (S_OK != hr)
    {
        m_lastError = QString("ConPty Error: UpdateProcThreadAttribute fail");
        DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
        free(startupInfo.lpAttributeList);
        if (hPipePTYIn != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYIn);
        if (hPipePTYOut != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYOut);
        if (INVALID_HANDLE_VALUE != m_hPipeOut) CloseHandle(m_hPipeOut);
        if (INVALID_HANDLE_VALUE != m_hPipeIn) CloseHandle(m_hPipeIn);
        m_ptyHandler = INVALID_HANDLE_VALUE;
        return false;
    }

    // Launch child process with bInheritHandles = FALSE (ConPTY spec requirement)
    PROCESS_INFORMATION piClient{};
    std::wstring currentDirStd = QDir::currentPath().toStdWString();
    hr = CreateProcessW(
                NULL,                           // No module name
                cmdArg.data(),                  // Command Line
                NULL,                           // Process handle not inheritable
                NULL,                           // Thread handle not inheritable
                FALSE,                          // Inherit handles (FALSE prevents handle conflict in ConPTY)
                EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,   // Creation flags with unicode env
                envArg,                         // Pass our custom environment block (containing TERM)
                currentDirStd.c_str(),          // Starting directory (prevent DLL pollution)
                &startupInfo.StartupInfo,       // Pointer to STARTUPINFO
                &piClient)                      // Pointer to PROCESS_INFORMATION
            ? S_OK
            : GetLastError();

    // The child has been spawned, PTY-ends of the pipes are safe to close in parent
    if (hPipePTYIn != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYIn);
    if (hPipePTYOut != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYOut);

    if (S_OK != hr)
    {
        m_lastError = QString("ConPty Error: Cannot create process -> %1").arg(hr);
        DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
        free(startupInfo.lpAttributeList);
        if (INVALID_HANDLE_VALUE != m_hPipeOut) CloseHandle(m_hPipeOut);
        if (INVALID_HANDLE_VALUE != m_hPipeIn) CloseHandle(m_hPipeIn);
        m_ptyHandler = INVALID_HANDLE_VALUE;
        return false;
    }
    m_pid = piClient.dwProcessId;

    // Check if the child process exited immediately (e.g. DLL init failed 0xc0000142)
    DWORD exitCode = 0;
    if (GetExitCodeProcess(piClient.hProcess, &exitCode)) {
        if (exitCode != STILL_ACTIVE) {
            qWarning() << "ConPty Error: Child process exited immediately with code: 0x" + QString::number(exitCode, 16);
            m_lastError = QString("Child process exited immediately with code: 0x%1").arg(exitCode, 0, 16);
            CloseHandle(piClient.hThread);
            CloseHandle(piClient.hProcess);
            DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
            free(startupInfo.lpAttributeList);
            if (INVALID_HANDLE_VALUE != m_hPipeOut) CloseHandle(m_hPipeOut);
            if (INVALID_HANDLE_VALUE != m_hPipeIn) CloseHandle(m_hPipeIn);
            m_ptyHandler = INVALID_HANDLE_VALUE;
            return false;
        }
    }

    // Read thread to asynchronously fetch output from the pipe
    m_readThread = QThread::create([this, piClient, startupInfo]()
    {
        while (!QThread::currentThread()->isInterruptionRequested())
        {
            DWORD dwAvail = 0;
            // 🌟 使用 PeekNamedPipe 预先检测管道中是否有数据可读。
            // 传入 INVALID_HANDLE_VALUE 会安全地返回 FALSE，而不会抛出异常。
            if (!PeekNamedPipe(m_hPipeIn, NULL, 0, NULL, &dwAvail, NULL))
            {
                // 管道损坏、关闭或句柄无效，安全退出读取循环
                break;
            }

            if (dwAvail == 0)
            {
                // 🌟 暂时无可用数据时挂起 10ms，防止空轮询跑满 CPU。
                // 避开同步阻塞 ReadFile，使得主线程可以在不引起 GDB 异常的前提下安全关闭句柄。
                QThread::msleep(10);
                continue;
            }

            const DWORD BUFF_SIZE{ 1024 };
            char szBuffer[BUFF_SIZE]{};
            DWORD dwBytesRead{};
            
            // 已确认有数据可读，此时 ReadFile 必会立即返回，绝无阻塞和 GDB 句柄解除冲突
            BOOL fRead = ReadFile(m_hPipeIn, szBuffer, BUFF_SIZE, &dwBytesRead, NULL);

            if (!fRead || dwBytesRead == 0)
            {
                // 读取失败或管道关闭，退出
                break;
            }

            {
                QMutexLocker locker(&m_bufferMutex);
                m_buffer.m_readBuffer.append(szBuffer, dwBytesRead);
            }
            m_buffer.emitReadyRead();
        }

        // Clean-up client app's process-info & thread handles
        CloseHandle(piClient.hThread);
        CloseHandle(piClient.hProcess);

        // Cleanup attribute list
        DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
        free(startupInfo.lpAttributeList);
    });

    m_readThread->start();
    return true;
}

bool ConPtyProcess::resize(qint16 cols, qint16 rows)
{
    if (m_ptyHandler == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool res = SUCCEEDED(m_winContext.resizePseudoConsole(m_ptyHandler, {cols, rows}));
    if (res)
    {
        m_size = QPair<qint16, qint16>(cols, rows);
    }
    return res;
}

bool ConPtyProcess::kill()
{
    bool exitCode = false;

    if (m_ptyHandler != INVALID_HANDLE_VALUE)
    {
        if (m_readThread)
        {
            m_readThread->requestInterruption();
            // Forcefully cancel blocking ReadFile on the pipe by closing the pipe handle
            if (m_hPipeIn != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_hPipeIn);
                m_hPipeIn = INVALID_HANDLE_VALUE;
            }
            m_readThread->quit();
            m_readThread->wait(); // 无限等待，直至子线程安全跳出 ReadFile 并结束
            delete m_readThread;  // 立即安全删除，避免延迟清理悬空
            m_readThread = nullptr;
        }

        // Close ConPTY - this will terminate the client process
        m_winContext.closePseudoConsole(m_ptyHandler);

        // Clean-up output pipe
        if (INVALID_HANDLE_VALUE != m_hPipeOut)
        {
            CloseHandle(m_hPipeOut);
            m_hPipeOut = INVALID_HANDLE_VALUE;
        }

        m_pid = 0;
        m_ptyHandler = INVALID_HANDLE_VALUE;
        exitCode = true;
    }

    return exitCode;
}

IPtyProcess::PtyType ConPtyProcess::type()
{
    return PtyType::ConPty;
}

QString ConPtyProcess::dumpDebugInfo()
{
#ifdef PTYQT_DEBUG
    return QString("PID: %1, Type: %2, Cols: %3, Rows: %4")
            .arg(m_pid).arg(type())
            .arg(m_size.first).arg(m_size.second);
#else
    return QString("Nothing...");
#endif
}

QIODevice *ConPtyProcess::notifier()
{
    return &m_buffer;
}

QByteArray ConPtyProcess::readAll()
{
    QMutexLocker locker(&m_bufferMutex);
    QByteArray data = m_buffer.m_readBuffer;
    m_buffer.m_readBuffer.clear();
    return data;
}

qint64 ConPtyProcess::write(const QByteArray &byteArray)
{
    if (m_hPipeOut == INVALID_HANDLE_VALUE)
        return 0;

    DWORD dwBytesWritten{};
    WriteFile(m_hPipeOut, byteArray.data(), byteArray.size(), &dwBytesWritten, NULL);
    return dwBytesWritten;
}

bool ConPtyProcess::isAvailable()
{
    qint32 buildNumber = QSysInfo::kernelVersion().split(".").last().toInt();
    if (buildNumber < CONPTY_MINIMAL_WINDOWS_VERSION)
        return false;
    return m_winContext.init();
}

void ConPtyProcess::moveToThread(QThread *targetThread)
{
    Q_UNUSED(targetThread);
}
