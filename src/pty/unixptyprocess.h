#ifndef UNIXPTYPROCESS_H
#define UNIXPTYPROCESS_H

#include "iptyprocess.h"
#include <QProcess>
#include <QSocketNotifier>

#ifdef Q_OS_UNIX

class ShellProcess : public QProcess
{
    friend class UnixPtyProcess;
    Q_OBJECT
public:
    ShellProcess(QObject *parent = nullptr) 
        : QProcess(parent)
        , m_handleMaster(-1)
        , m_handleSlave(-1)
    {
    }

    void emitReadyRead() { emit readyRead(); }

protected:
    void setupChildProcess() override;

private:
    int m_handleMaster;
    int m_handleSlave;
    QString m_handleSlaveName;
};

class UnixPtyProcess : public IPtyProcess
{
public:
    UnixPtyProcess();
    ~UnixPtyProcess() override;

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
    ShellProcess m_shellProcess;
    QSocketNotifier *m_readMasterNotify;
    QByteArray m_shellReadBuffer;
};

#endif // Q_OS_UNIX

#endif // UNIXPTYPROCESS_H
