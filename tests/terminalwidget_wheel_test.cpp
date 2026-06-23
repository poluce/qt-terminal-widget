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
    void syncCursorKeepsFollowingOutputWhenUserDidNotScroll();
    void syncCursorKeepsTopVisibleAfterPrimaryScreenClear();
    void heuristicAlternateScreenIsDisabledByDefault();
    void mousePressReportsMouseWhenTrackingEnabled();
    void mouseReleaseReportsMouseWhenTrackingEnabled();
    void bracketedPasteModeWrapsPastedText();
    void mouseTrackingCombinedModes();
    void keyReleaseEventWin32InputMode();
    void inputMethodEventCommitAndPreedit();
    void mouseTracking1005Mode();
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
    widget.m_mouseTrackingMode = 1000;
    widget.m_mouseEncoding = 1006;
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
    QApplication::processEvents();
    widget.m_isAlternateBuffer = false;
    widget.m_rows = 3;
    widget.m_cols = 80;
    widget.m_screenBufferStartRow = 7;
    widget.m_cursorRow = 3;
    widget.m_cursorCol = 1;

    widget.verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    widget.m_followTerminalOutput = false;

    QCOMPARE(widget.verticalScrollBar()->value(), 0);

    widget.syncCursor();

    QCOMPARE(widget.verticalScrollBar()->value(), 0);
}

void TerminalWidgetWheelTest::syncCursorKeepsFollowingOutputWhenUserDidNotScroll()
{
    TerminalWidget widget;
    widget.resize(320, 80);
    widget.show();
    QApplication::processEvents();

    widget.setPlainText("0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
    QApplication::processEvents();

    widget.m_isAlternateBuffer = false;
    widget.m_rows = 3;
    widget.m_cols = 80;
    widget.m_screenBufferStartRow = 7;
    widget.m_cursorRow = 3;
    widget.m_cursorCol = 1;
    widget.m_followTerminalOutput = true;

    widget.syncCursor();
    QApplication::processEvents();

    QCOMPARE(widget.verticalScrollBar()->value(), widget.verticalScrollBar()->maximum());

    widget.m_screenBufferStartRow = 8;
    widget.m_cursorRow = 3;
    widget.syncCursor();
    QApplication::processEvents();

    QCOMPARE(widget.verticalScrollBar()->value(), widget.verticalScrollBar()->maximum());
    QCOMPARE(widget.m_followTerminalOutput, true);
}

void TerminalWidgetWheelTest::syncCursorKeepsTopVisibleAfterPrimaryScreenClear()
{
    TerminalWidget widget;
    widget.resize(320, 80);
    widget.show();
    QApplication::processEvents();

    // 模拟主缓冲区内先有很多历史行，随后程序执行 clear/home，
    // 逻辑屏幕从顶端重新绘制当前一屏内容。
    widget.setPlainText("0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
    QApplication::processEvents();

    widget.m_isAlternateBuffer = false;
    widget.m_rows = 3;
    widget.m_cols = 80;
    widget.m_screenBufferStartRow = 0;
    widget.m_cursorRow = 1;
    widget.m_cursorCol = 1;
    widget.m_followTerminalOutput = true;

    widget.verticalScrollBar()->setValue(widget.verticalScrollBar()->maximum());
    QApplication::processEvents();

    widget.syncCursor();
    QApplication::processEvents();

    QCOMPARE(widget.verticalScrollBar()->value(), 0);
}

void TerminalWidgetWheelTest::heuristicAlternateScreenIsDisabledByDefault()
{
    QVERIFY(!TerminalWidget::shouldUseHeuristicAlternateScreen(true, false, true));
}

void TerminalWidgetWheelTest::mousePressReportsMouseWhenTrackingEnabled()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;

    widget.resize(800, 600);
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_mouseTrackingEnabled = true;
    widget.m_mouseTrackingMode = 1000;
    widget.m_mouseEncoding = 1006;
    widget.m_cols = 80;
    widget.m_rows = 24;

    QFontMetrics fm(widget.font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int cellH = qMax(1, fm.lineSpacing());

    // 模拟在 (2, 2) 单元格按下鼠标左键
    QPointF pos(cellW * 1.5, cellH * 1.5);
    QMouseEvent event(QEvent::MouseButtonPress,
                      pos,
                      pos,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);

    widget.mousePressEvent(&event);

    // 预期写入 PTY: \033[<0;2;2M
    QVERIFY(fakePty->writes.startsWith(QByteArray("\x1b[<0;2;2M")));
}

void TerminalWidgetWheelTest::mouseReleaseReportsMouseWhenTrackingEnabled()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;

    widget.resize(800, 600);
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_mouseTrackingEnabled = true;
    widget.m_mouseTrackingMode = 1000;
    widget.m_mouseEncoding = 1006;
    widget.m_cols = 80;
    widget.m_rows = 24;

    QFontMetrics fm(widget.font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int cellH = qMax(1, fm.lineSpacing());

    // 模拟在 (3, 4) 单元格松开鼠标左键
    QPointF pos(cellW * 2.5, cellH * 3.5);
    QMouseEvent event(QEvent::MouseButtonRelease,
                      pos,
                      pos,
                      Qt::LeftButton,
                      Qt::NoButton,
                      Qt::NoModifier);

    widget.mouseReleaseEvent(&event);

    // 预期写入 PTY: \033[<3;3;4m
    QVERIFY(fakePty->writes.startsWith(QByteArray("\x1b[<3;3;4m")));
}

void TerminalWidgetWheelTest::bracketedPasteModeWrapsPastedText()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;

    // 1. 测试未开启括号粘贴
    widget.m_bracketedPasteMode = false;
    QMimeData mimeData;
    mimeData.setText("hello");
    widget.insertFromMimeData(&mimeData);
    QCOMPARE(fakePty->writes, QByteArray("hello"));
    fakePty->writes.clear();

    // 2. 测试开启括号粘贴
    widget.m_bracketedPasteMode = true;
    widget.insertFromMimeData(&mimeData);
    QCOMPARE(fakePty->writes, QByteArray("\033[200~hello\033[201~"));
    fakePty->writes.clear();

    // 3. 测试 stopShell 后的重置
    widget.stopShell();
    QCOMPARE(widget.m_bracketedPasteMode, false);
    QCOMPARE(widget.m_mouseTrackingEnabled, false);
    QCOMPARE(widget.m_mouseTrackingMode, 0);
    QCOMPARE(widget.m_mouseEncoding, 0);
}

void TerminalWidgetWheelTest::mouseTrackingCombinedModes()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;
    widget.resize(800, 600);
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_cols = 80;
    widget.m_rows = 24;

    QFontMetrics fm(widget.font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int cellH = qMax(1, fm.lineSpacing());

    // 组合 1：开启 ?1002 (拖拽追踪) + ?1006 (SGR 编码)
    widget.m_mouseTrackingEnabled = true;
    widget.m_mouseTrackingMode = 1002;
    widget.m_mouseEncoding = 1006;

    // 模拟无按键的移动事件，应该不报告
    QPointF pos1(cellW * 5.5, cellH * 5.5);
    QMouseEvent moveNoBtn(QEvent::MouseMove, pos1, pos1, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    widget.mouseMoveEvent(&moveNoBtn);
    QCOMPARE(fakePty->writes, QByteArray());

    // 模拟左键拖拽（有按键的移动事件），应该报告
    QMouseEvent moveWithBtn(QEvent::MouseMove, pos1, pos1, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    widget.mouseMoveEvent(&moveWithBtn);
    QVERIFY(!fakePty->writes.isEmpty());
    QVERIFY(fakePty->writes.contains("\033[<32;6;6M"));
    fakePty->writes.clear();

    // 组合 2：开启 ?1003 (全移动追踪) + ?1006 (SGR 编码)
    widget.m_mouseTrackingMode = 1003;
    widget.mouseMoveEvent(&moveNoBtn);
    QVERIFY(!fakePty->writes.isEmpty());
    QVERIFY(fakePty->writes.contains("\033[<35;6;6M"));
    fakePty->writes.clear();

    // 组合 3：开启 ?1000 (普通按下/松开) + ?1006 (SGR 编码)
    widget.m_mouseTrackingMode = 1000;
    widget.mouseMoveEvent(&moveWithBtn); // 1000 模式移动不报告
    QCOMPARE(fakePty->writes, QByteArray());
}

void TerminalWidgetWheelTest::keyReleaseEventWin32InputMode()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;

    // 1. 开启 Win32 input mode
    widget.m_win32InputModeActive = true;

    // 模拟松开 'A' 键 (VK_A = 0x41)
    QKeyEvent releaseEvent(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a");
    widget.keyReleaseEvent(&releaseEvent);

    // 应该在 PTY 写入 keyDown=0 的 Win32 报告，通常是 \033[65;...
    QVERIFY(!fakePty->writes.isEmpty());
    QVERIFY(fakePty->writes.startsWith("\033[65;"));
    fakePty->writes.clear();

    // 2. 关闭 Win32 input mode
    widget.m_win32InputModeActive = false;
    widget.keyReleaseEvent(&releaseEvent);
    QCOMPARE(fakePty->writes, QByteArray());
}

void TerminalWidgetWheelTest::inputMethodEventCommitAndPreedit()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_cursorRow = 1;
    widget.m_cursorCol = 1;

    // 1. 模拟 IME 预编辑拼音（被终端拦截并存储在网格）
    QInputMethodEvent preeditEvent("zhong", QList<QInputMethodEvent::Attribute>());
    widget.inputMethodEvent(&preeditEvent);
    QCOMPARE(fakePty->writes, QByteArray()); // 不应向 PTY 写入
    QCOMPARE(widget.m_currentPreeditString, QString("zhong"));
    QCOMPARE(widget.m_inPreedit, true);
    // 逻辑光标应当偏置到 preedit 字符串的末尾
    QCOMPARE(widget.m_cursorCol, 1 + 5);

    // 2. 验证 inputMethodQuery 返回的是真实的逻辑光标处的物理像素矩形
    QVariant cursorRectVal = widget.inputMethodQuery(Qt::ImCursorRectangle);
    QVERIFY(cursorRectVal.isValid());
    QCOMPARE(cursorRectVal.userType(), QMetaType::QRect);
    
    QFontMetrics fm(widget.font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int cellH = qMax(1, fm.lineSpacing());
    // 逻辑光标在 (1, 6)，所以物理原点应该在 x = 5 * cellW, y = 0
    QRect expectedRect(5 * cellW, 0, cellW, cellH);
    QCOMPARE(cursorRectVal.toRect(), expectedRect);

    // 3. 模拟 IME 提交最终字符 "中"
    QInputMethodEvent commitEvent;
    commitEvent.setCommitString("中");
    widget.inputMethodEvent(&commitEvent);
    // 应该将最终字符提交给 PTY
    QCOMPARE(fakePty->writes, QString("中").toUtf8());
    // 预编辑状态应该被彻底清空
    QCOMPARE(widget.m_inPreedit, false);
    QCOMPARE(widget.m_currentPreeditString, QString());
    // 逻辑光标退回到 Anchor 起始位置 (1, 1)，等候 PTY 回显来真正驱动光标移动
    QCOMPARE(widget.m_cursorCol, 1);
}

void TerminalWidgetWheelTest::mouseTracking1005Mode()
{
    TerminalWidget widget;
    auto *fakePty = new FakePtyProcess;
    widget.resize(800, 600);
    widget.m_pty = fakePty;
    widget.m_isShellRunning = true;
    widget.m_cols = 250;
    widget.m_rows = 250;

    QFontMetrics fm(widget.font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int cellH = qMax(1, fm.lineSpacing());

    // 启用 1005 模式和大坐标 (200, 200)
    widget.m_mouseTrackingEnabled = true;
    widget.m_mouseTrackingMode = 1000;
    widget.m_mouseEncoding = 1005;

    // 模拟在 (200, 200) 点击
    QPointF pos(cellW * 199.5, cellH * 199.5);
    QMouseEvent event(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    widget.mousePressEvent(&event);

    // 预期的 C_b = 32, C_x = 232, C_y = 232
    // 在 UTF-8 下，32 对应 ' ' 字节 0x20
    // 232 对应 Unicode 码点 232，其 UTF-8 为 0xC3 0xA8
    // 预期写入 PTY: \033[M (0x1b 0x5b 0x4d) + ' ' (0x20) + 0xC3 0xA8 + 0xC3 0xA8
    QByteArray expected;
    expected.append("\033[M ");
    
    QString coords;
    coords.append(QChar(232));
    coords.append(QChar(232));
    expected.append(coords.toUtf8());

    QCOMPARE(fakePty->writes, expected);
}


QTEST_MAIN(TerminalWidgetWheelTest)
#include "terminalwidget_wheel_test.moc"
