#include <QtTest/QtTest>
#include <QBuffer>
#include <QScrollBar>
#include <QWheelEvent>

#define private public
#define protected public
#include "terminal/terminalwidget.h"
#undef protected
#undef private

class FakePtyProcess final : public IPtyProcess
{
public:
    bool startProcess(const QString &, QStringList, qint16, qint16) override { return true; }
    bool resize(qint16, qint16) override { return true; }
    bool kill() override { return true; }
    PtyType type() override { return ConPty; }
    QString dumpDebugInfo() override { return {}; }
    QIODevice *notifier() override { return &m_notifier; }
    QByteArray readAll() override { return {}; }
    qint64 write(const QByteArray &byteArray) override
    {
        writes.append(byteArray);
        return byteArray.size();
    }
    bool isAvailable() override { return true; }
    void moveToThread(QThread *targetThread) override { m_notifier.moveToThread(targetThread); }

    QByteArray writes;

private:
    QBuffer m_notifier;
};

class TerminalWidgetWheelTest : public QObject
{
    Q_OBJECT

private slots:
    void alternateBufferWheelDoesNotBecomeArrowKeysWithoutMouseTracking();
    void alternateBufferWheelReportsMouseWhenTrackingEnabled();
    void syncCursorPreservesPrimaryScrollPositionAfterUserScroll();
};

void TerminalWidgetWheelTest::alternateBufferWheelDoesNotBecomeArrowKeysWithoutMouseTracking()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;

    widget.resize(800, 600);
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_isAlternateBuffer = true;
    widget.m_mouseTrackingEnabled = false;
    widget.m_cols = 80;
    widget.m_rows = 24;

    QWheelEvent event(QPointF(0.0, 0.0),
                      QPointF(0.0, 0.0),
                      QPoint(),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);

    widget.wheelEvent(&event);

    QCOMPARE(fakePty->writes, QByteArray());
}

void TerminalWidgetWheelTest::alternateBufferWheelReportsMouseWhenTrackingEnabled()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;

    widget.resize(800, 600);
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_isAlternateBuffer = true;
    widget.m_mouseTrackingEnabled = true;
    widget.m_mouseProtocol = 1006;
    widget.m_cols = 80;
    widget.m_rows = 24;

    QWheelEvent event(QPointF(10.0, 10.0),
                      QPointF(10.0, 10.0),
                      QPoint(),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);

    widget.wheelEvent(&event);

    QVERIFY(fakePty->writes.startsWith(QByteArray("\x1b[<64;")));
    QVERIFY(fakePty->writes.endsWith('M'));
}

void TerminalWidgetWheelTest::syncCursorPreservesPrimaryScrollPositionAfterUserScroll()
{
    TerminalWidget widget;
    widget.resize(320, 80);
    widget.show();
    QApplication::processEvents();

    widget.setPlainText("0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
    widget.m_isAlternateBuffer = false;
    widget.m_rows = 3;
    widget.m_cols = 80;
    widget.m_screenBufferStartRow = 7;
    widget.m_cursorRow = 3;
    widget.m_cursorCol = 1;

    widget.verticalScrollBar()->setValue(0);
    QApplication::processEvents();

    QCOMPARE(widget.verticalScrollBar()->value(), 0);

    widget.syncCursor();

    QCOMPARE(widget.verticalScrollBar()->value(), 0);
}

QTEST_MAIN(TerminalWidgetWheelTest)
#include "terminalwidget_wheel_test.moc"
