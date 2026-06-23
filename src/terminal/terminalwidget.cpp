#include "terminalwidget.h"
#include "inputtranslator.h"
#include <QKeyEvent>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QScrollBar>
#include <QProcessEnvironment>
#include <QFontDatabase>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextBlock>
#include <QPlainTextDocumentLayout>
#include "pty/ptyqt.h"
#include <windows.h>
#include <tlhelp32.h>

namespace {
bool hasActiveChildProcess(qint64 parentPid) {
    if (parentPid <= 0) return false;
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    bool hasChild = false;
    if (Process32First(hSnapshot, &pe)) {
        do {
            if (pe.th32ParentProcessID == (DWORD)parentPid) {
                QString exeName = QString::fromWCharArray(pe.szExeFile).toLower();
                if (exeName != "conhost.exe" && exeName != "openconsole.exe") {
                    hasChild = true;
                    break;
                }
            }
        } while (Process32Next(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return hasChild;
}

const char *tokenTypeName(AnsiToken::Type type)
{
    switch (type) {
    case AnsiToken::Text: return "Text";
    case AnsiToken::ClearScreen: return "ClearScreen";
    case AnsiToken::ClearLine: return "ClearLine";
    case AnsiToken::CursorPosition: return "CursorPosition";
    case AnsiToken::Backspace: return "Backspace";
    case AnsiToken::CursorUp: return "CursorUp";
    case AnsiToken::CursorDown: return "CursorDown";
    case AnsiToken::CursorForward: return "CursorForward";
    case AnsiToken::CursorBackward: return "CursorBackward";
    case AnsiToken::CursorNextLine: return "CursorNextLine";
    case AnsiToken::CursorPrevLine: return "CursorPrevLine";
    case AnsiToken::CursorHorizontalAbsolute: return "CursorHorizontalAbsolute";
    case AnsiToken::SaveCursor: return "SaveCursor";
    case AnsiToken::RestoreCursor: return "RestoreCursor";
    case AnsiToken::EraseInDisplay: return "EraseInDisplay";
    case AnsiToken::EraseInLine: return "EraseInLine";
    case AnsiToken::DeleteCharacter: return "DeleteCharacter";
    case AnsiToken::EraseCharacter: return "EraseCharacter";
    case AnsiToken::InsertLine: return "InsertLine";
    case AnsiToken::DeleteLine: return "DeleteLine";
    case AnsiToken::HideCursor: return "HideCursor";
    case AnsiToken::ShowCursor: return "ShowCursor";
    case AnsiToken::EnterAlternateBuffer: return "EnterAlternateBuffer";
    case AnsiToken::ExitAlternateBuffer: return "ExitAlternateBuffer";
    case AnsiToken::Win32InputModeSet: return "Win32InputModeSet";
    case AnsiToken::Win32InputModeReset: return "Win32InputModeReset";
    case AnsiToken::MouseTrackingSet: return "MouseTrackingSet";
    case AnsiToken::MouseTrackingReset: return "MouseTrackingReset";
    case AnsiToken::DeviceAttributesQuery: return "DeviceAttributesQuery";
    case AnsiToken::SecondaryDeviceAttributesQuery: return "SecondaryDeviceAttributesQuery";
    case AnsiToken::BracketedPasteSet: return "BracketedPasteSet";
    case AnsiToken::BracketedPasteReset: return "BracketedPasteReset";
    case AnsiToken::MouseEncodingSet: return "MouseEncodingSet";
    case AnsiToken::MouseEncodingReset: return "MouseEncodingReset";
    }
    return "Unknown";
}

QString summarizeTokens(const QList<AnsiToken> &tokens)
{
    QStringList parts;
    const int limit = qMin(tokens.size(), 8);
    for (int i = 0; i < limit; ++i) {
        const AnsiToken &token = tokens.at(i);
        QString part = QString::fromLatin1(tokenTypeName(token.type));
        if (token.type == AnsiToken::Text) {
            part += QString("(%1)").arg(token.text.size());
        } else if (token.val1 >= 0) {
            part += QString("(%1)").arg(token.val1);
        }
        parts.append(part);
    }
    if (tokens.size() > limit) {
        parts.append(QString("+%1 more").arg(tokens.size() - limit));
    }
    return parts.join(", ");
}
}


TerminalWidget::TerminalWidget(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_pty(nullptr)
    , m_isShellRunning(false)
    , m_cursorVisible(true)
    , m_cursorRow(1)
    , m_cursorCol(1)
    , m_cols(80)
    , m_rows(24)
    , m_savedRow(1)
    , m_savedCol(1)
    , m_pendingShellPath("")
    , m_screenBufferStartRow(0)
    , m_primaryDoc(nullptr)
    , m_alternateDoc(nullptr)
    , m_isAlternateBuffer(false)
    , m_win32InputModeActive(false)
    , m_isHeuristicAlternateBuffer(false)
    , m_mouseTrackingEnabled(false)
    , m_mouseTrackingMode(0)
    , m_mouseEncoding(0)
    , m_followTerminalOutput(true)
    , m_syncingScrollBar(0)
    , m_bracketedPasteMode(false)
    , m_inPreedit(false)
    , m_preeditAnchorRow(0)
    , m_preeditAnchorCol(0)
{
    setupUi();
}

TerminalWidget::~TerminalWidget()
{
    // 🌟 静默关闭 PTY，避免在 widget 析构过程中触发任何 GUI/文档重绘操作以防崩溃
    if (m_pty) {
        m_pty->kill();
        delete m_pty;
        m_pty = nullptr;
    }
    if (m_alternateDoc) {
        delete m_alternateDoc;
    }
}

void TerminalWidget::setupUi()
{
    // Configure rich dark aesthetics for professional look
    QPalette p = palette();
    p.setColor(QPalette::Base, QColor(20, 20, 20));       // Near black background
    p.setColor(QPalette::Text, QColor(220, 220, 220));    // Off-white text
    setPalette(p);

    // Setup monospace font
    QFont font;
#ifdef Q_OS_WIN
    // 优先使用精美的 Cascadia Mono 或 Consolas 终端字体，缺失字符自动且优先 fallback 到等宽的 NSimSun
    font.setFamilies({"Cascadia Mono", "Consolas", "NSimSun"});
#else
    font.setFamily("Monospace");
#endif
    font.setPointSize(10);
    font.setKerning(false);
    font.setStyleHint(QFont::Monospace);
    setFont(font);

    // Ensure tab stops are strictly aligned to 8 character columns
    QFontMetrics fm(font);
    int charWidth = fm.horizontalAdvance('A');
    setTabStopDistance(charWidth * 8);

    // Disable line wrap for standard terminal column-wise alignment
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    
    // Customize scroll bars for modern look
    verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: #1e1e1e; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #4f4f4f; min-height: 20px; border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: #686868; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );
    horizontalScrollBar()->setStyleSheet(
        "QScrollBar:horizontal { background: #1e1e1e; height: 10px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: #4f4f4f; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal:hover { background: #686868; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
    );

    // 彻底清空边距以保证像素网格完全对齐
    setViewportMargins(0, 0, 0, 0);
    document()->setDocumentMargin(0);
    setStyleSheet("border: none; padding: 0;");

    // 显式启用强焦点机制
    setFocusPolicy(Qt::StrongFocus);
    m_primaryDoc = document();

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_syncingScrollBar > 0 || m_isAlternateBuffer) {
            return;
        }
        const bool oldFollow = m_followTerminalOutput;
        m_followTerminalOutput = (value >= verticalScrollBar()->maximum());
        if (oldFollow != m_followTerminalOutput) {
            QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] valueChanged"
                                             << "value =" << value
                                             << "maximum =" << verticalScrollBar()->maximum()
                                             << "follow =" << m_followTerminalOutput;
            traceViewportState("scrollbar-valueChanged");
        }
    });
}

bool TerminalWidget::startShell(const QString &shellPath)
{
    QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell called with path:" << shellPath
                             << "viewport width:" << viewport()->width() << "height:" << viewport()->height();

    if (m_isShellRunning) {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell: shell is already running.";
        return false;
    }

    // 检测是否拥有合理的物理大小。若未准备就绪，开启延迟启动（Lazy Start）防止初始超小尺寸导致输出截断
    if (viewport()->width() < 200 || viewport()->height() < 100) {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell: pending startup due to small viewport size.";
        m_pendingShellPath = shellPath.isEmpty() ? "default" : shellPath;
        return true;
    }

    QString realShellPath = shellPath;
    if (realShellPath.isEmpty()) {
#ifdef Q_OS_WIN
        // 动态读取系统默认命令提示符路径，消除绝对物理路径硬编码
        realShellPath = QProcessEnvironment::systemEnvironment().value("COMSPEC");
        if (realShellPath.isEmpty()) {
            realShellPath = "cmd.exe";
        }
#else
        realShellPath = QProcessEnvironment::systemEnvironment().value("SHELL");
        if (realShellPath.isEmpty()) {
            realShellPath = "/bin/sh";
        }
#endif
    }

    // 如果是相对路径，则在系统环境变量 PATH 中进行搜索
    if (QFileInfo(realShellPath).isRelative()) {
        QString absPath = QStandardPaths::findExecutable(realShellPath);
        if (!absPath.isEmpty()) {
            realShellPath = absPath;
        } else {
            // Fallback 策略：针对 Windows 常见相对路径进行默认搜寻
#ifdef Q_OS_WIN
            if (realShellPath.compare("powershell.exe", Qt::CaseInsensitive) == 0) {
                QString systemRoot = QProcessEnvironment::systemEnvironment().value("SystemRoot", "C:\\Windows");
                QString defaultPowerShell = systemRoot + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
                if (QFile::exists(defaultPowerShell)) {
                    realShellPath = defaultPowerShell;
                }
            } else if (realShellPath.compare("cmd.exe", Qt::CaseInsensitive) == 0) {
                QString systemRoot = QProcessEnvironment::systemEnvironment().value("SystemRoot", "C:\\Windows");
                QString defaultCmd = systemRoot + "\\System32\\cmd.exe";
                if (QFile::exists(defaultCmd)) {
                    realShellPath = defaultCmd;
                }
            }
#endif
        }
    }

    // 检查最终得到的路径是否是绝对路径，以及物理上是否存在
    QFileInfo checkFi(realShellPath);
    if (checkFi.isRelative() || !QFile::exists(realShellPath)) {
        qWarning() << "[TRACE] startShell FAILED: Absolute shell path cannot be resolved for" << shellPath 
                   << ", resolved path:" << realShellPath;
        return false;
    }
    QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell: resolved shell path =" << realShellPath;

    m_pty = PtyQt::createPtyProcess();
    if (!m_pty) {
        qWarning() << "[TRACE] startShell: Failed to create Pty process instance.";
        return false;
    }

    // Determine starting terminal dimensions using system font metrics
    QFontMetrics fm(font());
    int charWidth = qMax(1, fm.horizontalAdvance('A'));
    int charHeight = qMax(1, fm.lineSpacing());
    int cols = viewport()->width() / charWidth;
    int rows = viewport()->height() / charHeight;

    if (cols < 40) cols = 40;
    if (rows < 10) rows = 10;

    m_cols = cols;
    m_rows = rows;
    QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell: starting process with cols =" << cols << "rows =" << rows;

    // Load environment and explicitly inject terminal capability variables
    QProcessEnvironment procEnv = QProcessEnvironment::systemEnvironment();
    procEnv.insert("TERM", "xterm-256color");
    procEnv.insert("COLORTERM", "truecolor");
    QStringList env = procEnv.toStringList();

    // Start process
    bool ok = m_pty->startProcess(realShellPath, env, cols, rows);
    if (!ok) {
        qWarning() << "[TRACE] startShell: m_pty->startProcess FAILED. Error:" << m_pty->lastError();
        delete m_pty;
        m_pty = nullptr;
        return false;
    }

    m_isShellRunning = true;
    QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell: process started successfully. PID debug info:" << m_pty->dumpDebugInfo();

    m_parser.reset();
    clear();
    {
        QTextDocument *doc = document();
        QTextCursor cursor(doc);
        QString initialBlank;
        for (int r = 1; r < m_rows; ++r) {
            initialBlank += "\n";
        }
        cursor.insertText(initialBlank);
    }

    // Connect readyRead signal using QueuedConnection to guarantee thread-safe GUI updates on the main thread
    connect(m_pty->notifier(), &QIODevice::readyRead, this, &TerminalWidget::onPtyReadyRead, Qt::QueuedConnection);
    QT_TERMINAL_TRACE_STREAM << "[TRACE] startShell: connected readyRead signal with QueuedConnection.";

    setFocus();
    return true;
}

void TerminalWidget::stopShell()
{
    clearPreedit();
    if (m_pty) {
        m_pty->kill();
        delete m_pty;
        m_pty = nullptr;
    }
    m_isShellRunning = false;
    m_win32InputModeActive = false;
    m_isHeuristicAlternateBuffer = false;
    m_isAlternateBuffer = false;
    m_bracketedPasteMode = false;
    m_mouseTrackingEnabled = false;
    m_mouseTrackingMode = 0;
    m_mouseEncoding = 0;
    setMouseTracking(false);
    m_followTerminalOutput = true;
}

void TerminalWidget::onPtyReadyRead()
{
    if (!m_pty) {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] onPtyReadyRead called but m_pty is NULL.";
        return;
    }

    QByteArray data = m_pty->readAll();
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] onPtyReadyRead: read bytes =" << data.size() << "hex =" << data.toHex();
    if (data.isEmpty()) return;

    QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] readyRead begin"
                                     << "bytes =" << data.size()
                                     << "follow =" << m_followTerminalOutput
                                     << "scroll =" << verticalScrollBar()->value()
                                     << "maximum =" << verticalScrollBar()->maximum()
                                     << "startRow =" << m_screenBufferStartRow
                                     << "cursor =" << m_cursorRow << m_cursorCol;

    QString savedPreedit = m_currentPreeditString;
    bool wasInPreedit = m_inPreedit;
    if (wasInPreedit) {
        clearPreedit();
    }

    // 🌟 记录 PTY 数据写入前的滚动条位置和旧起始行，以便后面做精确的历史浏览滚动补偿
    int preservedScrollValue = verticalScrollBar()->value();
    int oldStartRow = m_screenBufferStartRow;

    // 🌟 开启保护，防止数据解析和光标定位引起 valueChanged 信号强行将 m_followTerminalOutput 改为 true
    m_syncingScrollBar++;

    QList<AnsiToken> tokens = m_parser.parse(data);
    QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] readyRead tokens"
                                     << "count =" << tokens.size()
                                     << "summary =" << summarizeTokens(tokens);

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);

    for (const AnsiToken &token : tokens) {
        handleToken(token);
    }

    cursor.endEditBlock();
    syncCursor();

    // 🌟 计算在此期间由于自动换行或命令输出导致的主屏幕内容自增行数
    int startRowDelta = m_screenBufferStartRow - oldStartRow;

    // 🌟 如果当前是主屏且是非跟随（历史浏览）状态，补偿滚动条位置以确保历史文本视图物理上绝对静止
    if (!m_isAlternateBuffer && !m_followTerminalOutput) {
        preservedScrollValue += startRowDelta;
        verticalScrollBar()->setValue(preservedScrollValue);
    }

    // 🌟 恢复同步保护标志
    m_syncingScrollBar--;

    if (wasInPreedit && !savedPreedit.isEmpty()) {
        applyPreedit(savedPreedit);
    }

    QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] readyRead end"
                                     << "startRowDelta =" << startRowDelta
                                     << "follow =" << m_followTerminalOutput
                                     << "scroll =" << verticalScrollBar()->value()
                                     << "maximum =" << verticalScrollBar()->maximum();
    traceViewportState("readyRead-end");
}

void TerminalWidget::writeTextSegment(const QString &textToInsert, const QTextCharFormat &format)
{
    if (textToInsert.isEmpty()) return;

    QFontMetrics fm(font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));

    int i = 0;
    int len = textToInsert.length();

    struct TextSegment {
        QString text;
        int stretch;
    };
    QList<TextSegment> segments;

    while (i < len) {
        QChar ch = textToInsert.at(i);
        QString charStr;
        int logicalW = 1;

        if (ch.isHighSurrogate() && i + 1 < len) {
            charStr = textToInsert.mid(i, 2);
            int pixelWidth = fm.horizontalAdvance(charStr);
            logicalW = qMax(1, (pixelWidth + cellW / 2) / cellW);
            i += 2;
        } else {
            charStr = QString(ch);
            logicalW = charDisplayWidth(ch);
            i += 1;
        }

        int actualW = fm.horizontalAdvance(charStr);
        int expectedW = logicalW * cellW;
        int stretch = 100;

        if (actualW > expectedW && actualW > 0) {
            stretch = (expectedW * 100) / actualW;
            if (stretch < 10) stretch = 10;
            if (stretch > 100) stretch = 100;
        }

        if (!segments.isEmpty() && segments.last().stretch == stretch) {
            segments.last().text.append(charStr);
        } else {
            segments.append({charStr, stretch});
        }
    }

    for (const TextSegment &seg : segments) {
        QTextCharFormat segFormat = format;
        if (seg.stretch != 100) {
            segFormat.setFontStretch(seg.stretch);
        }
        writeTextSegmentInternal(seg.text, segFormat);
    }
}

void TerminalWidget::writeTextSegmentInternal(const QString &textToInsert, const QTextCharFormat &format)
{
    if (textToInsert.isEmpty()) return;

    QTextDocument *doc = document();
    int i = 0;
    int len = textToInsert.length();

    while (i < len) {
        QString charStr;
        int logicalW = 1;
        QChar ch = textToInsert.at(i);

        if (ch.isHighSurrogate() && i + 1 < len) {
            charStr = textToInsert.mid(i, 2);
            logicalW = stringDisplayWidth(charStr);
            i += 2;
        } else {
            charStr = QString(ch);
            logicalW = charDisplayWidth(ch);
            i += 1;
        }

        // 1. 自动折行判定 (Auto-Wrap / DECAWM 行为仿真)
        bool needWrap = false;
        if (m_cursorCol > m_cols) {
            needWrap = true;
        } else if (logicalW > 1 && (m_cursorCol + logicalW - 1 > m_cols)) {
            // 宽字符在当前行末尾已容纳不下，在当前位置补齐空格，并让该宽字符换到下一行行首写入
            needWrap = true;
            int fillSpaces = m_cols - m_cursorCol + 1;
            if (fillSpaces > 0) {
                writeSingleCharString(QString(fillSpaces, ' '), fillSpaces, QTextCharFormat());
            }
        }

        if (needWrap) {
            m_cursorCol = 1;
            m_cursorRow++;
            if (m_cursorRow > m_rows) {
                m_cursorRow = m_rows;
                if (!m_isAlternateBuffer) {
                    QTextCursor endCursor(doc);
                    endCursor.movePosition(QTextCursor::End);
                    endCursor.insertText("\n");
                    m_screenBufferStartRow++;
                }
            }
        }

        // 2. 物理写入单个逻辑字宽的字符块
        writeSingleCharString(charStr, logicalW, format);
    }
}

void TerminalWidget::writeSingleCharString(const QString &textToInsert, int logicalW, const QTextCharFormat &format)
{
    QTextDocument *doc = document();
    
    // 补齐空行以保证能容纳当前屏幕
    while (doc->blockCount() < m_screenBufferStartRow + m_rows) {
        QTextCursor endCursor(doc);
        endCursor.movePosition(QTextCursor::End);
        endCursor.insertText("\n");
    }

    // 绝对物理行号映射
    int docRow = m_screenBufferStartRow + m_cursorRow - 1;
    QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
    QTextCursor writeCursor(block);

    // 物理列宽填充
    int currentWidth = blockDisplayWidth(block);
    if (currentWidth < m_cursorCol - 1) {
        writeCursor.movePosition(QTextCursor::EndOfBlock);
        writeCursor.insertText(QString(m_cursorCol - 1 - currentWidth, ' '), QTextCharFormat());
    }

    int charIdx = blockColumnToCharIndex(block, m_cursorCol);
    writeCursor.setPosition(block.position() + charIdx);

    // 覆盖写入字符替换与宽字符边缘填充
    int insertWidth = logicalW;
    int charsToRemove = 0;
    int paddingSpaces = 0;
    int startCol = m_cursorCol;
    int endCol = startCol + insertWidth - 1;
    int currentCol = startCol;
    QString blockText = block.text();

    for (int j = charIdx; j < blockText.length(); ) {
        if (currentCol > endCol) {
            break;
        }
        int w = blockCharDisplayWidthAt(block, j);
        int logicalLen = (j < blockText.length() - 1 && blockText.at(j).isHighSurrogate()) ? 2 : 1;
        if (currentCol <= endCol) {
            charsToRemove += logicalLen;
            int rightBoundary = currentCol + w - 1;
            if (rightBoundary > endCol) {
                paddingSpaces = rightBoundary - endCol;
            }
        }
        currentCol += w;
        j += logicalLen;
    }

    if (charsToRemove > 0) {
        writeCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, charsToRemove);
        writeCursor.removeSelectedText();
    }

    writeCursor.insertText(textToInsert, format);

    if (paddingSpaces > 0) {
        writeCursor.insertText(QString(paddingSpaces, ' '), QTextCharFormat());
    }

    m_cursorCol += insertWidth;
}

void TerminalWidget::handleToken(const AnsiToken &token)
{
    switch (token.type) {
    case AnsiToken::EnterAlternateBuffer: {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] handleToken: ENTER ALTERNATE BUFFER. Previous mode:" << m_isAlternateBuffer;
        if (!m_isAlternateBuffer) {
            m_primaryDoc = document();
            if (!m_alternateDoc) {
                m_alternateDoc = new QTextDocument(this);
                m_alternateDoc->setDocumentLayout(new QPlainTextDocumentLayout(m_alternateDoc));
                m_alternateDoc->setDefaultFont(font());
            }
            m_isAlternateBuffer = true;
            setDocument(m_alternateDoc);

            m_alternateDoc->clear();
            QTextCursor cursor(m_alternateDoc);
            QString initialBlank;
            for (int r = 1; r < m_rows; ++r) {
                initialBlank += "\n";
            }
            cursor.insertText(initialBlank);

            m_cursorRow = 1;
            m_cursorCol = 1;
            m_screenBufferStartRow = 0;

            verticalScrollBar()->setVisible(false);
            verticalScrollBar()->setRange(0, 0);
        }
        break;
    }
    case AnsiToken::ExitAlternateBuffer: {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] handleToken: EXIT ALTERNATE BUFFER. Previous mode:" << m_isAlternateBuffer;
        if (m_isAlternateBuffer) {
            m_isAlternateBuffer = false;
            setDocument(m_primaryDoc);

            verticalScrollBar()->setVisible(true);
            m_cursorRow = qBound(1, m_cursorRow, m_rows);
            m_cursorCol = qBound(1, m_cursorCol, m_cols);

            QTextDocument *doc = document();
            if (doc->blockCount() > m_rows) {
                m_screenBufferStartRow = doc->blockCount() - m_rows;
            } else {
                m_screenBufferStartRow = 0;
            }
            syncCursor();
        }
        break;
    }
    case AnsiToken::CursorPosition: {
        QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] CursorPosition"
                                         << "row =" << token.cursorRow
                                         << "col =" << token.cursorCol;
        m_cursorRow = qBound(1, token.cursorRow, m_rows);
        m_cursorCol = qBound(1, token.cursorCol, m_cols);
        break;
    }
    case AnsiToken::CursorUp: {
        m_cursorRow = qMax(1, m_cursorRow - token.val1);
        break;
    }
    case AnsiToken::CursorDown: {
        m_cursorRow = qMin(m_rows, m_cursorRow + token.val1);
        break;
    }
    case AnsiToken::CursorForward: {
        m_cursorCol = qMin(m_cols, m_cursorCol + token.val1);
        break;
    }
    case AnsiToken::CursorBackward: {
        m_cursorCol = qMax(1, m_cursorCol - token.val1);
        break;
    }
    case AnsiToken::CursorNextLine: {
        m_cursorRow = qMin(m_rows, m_cursorRow + token.val1);
        m_cursorCol = 1;
        break;
    }
    case AnsiToken::CursorPrevLine: {
        m_cursorRow = qMax(1, m_cursorRow - token.val1);
        m_cursorCol = 1;
        break;
    }
    case AnsiToken::CursorHorizontalAbsolute: {
        m_cursorCol = qBound(1, token.val1, m_cols);
        break;
    }
    case AnsiToken::SaveCursor: {
        m_savedRow = m_cursorRow;
        m_savedCol = m_cursorCol;
        break;
    }
    case AnsiToken::RestoreCursor: {
        m_cursorRow = m_savedRow;
        m_cursorCol = m_savedCol;
        break;
    }
    case AnsiToken::Text: {
        QString rawText = token.text;
        QString cleanText;
        cleanText.reserve(rawText.length());
        for (int j = 0; j < rawText.length(); ++j) {
            QChar ch = rawText.at(j);
            ushort u = ch.unicode();
            if (u < 0x20 && u != '\n' && u != '\t' && u != '\r') {
                continue; // 强力过滤 C0 不可见控制码，放行 \n, \t, \r
            }
            cleanText.append(ch);
        }
        if (cleanText.isEmpty()) break;

        QTextDocument *doc = document();
        int start = 0;
        int len = cleanText.length();
        for (int j = 0; j < len; ++j) {
            QChar ch = cleanText.at(j);
            if (ch == '\n' || ch == '\r' || ch == '\t') {
                if (j > start) {
                    QString textToInsert = cleanText.mid(start, j - start);
                    writeTextSegment(textToInsert, token.format);
                }
                if (ch == '\n') {
                    m_cursorCol = 1;
                    m_cursorRow++;
                    if (m_cursorRow > m_rows) {
                        m_cursorRow = m_rows;
                        if (!m_isAlternateBuffer) {
                            QTextCursor endCursor(doc);
                            endCursor.movePosition(QTextCursor::End);
                            endCursor.insertText("\n");
                            m_screenBufferStartRow++;
                        }
                    }
                } else if (ch == '\r') {
                    m_cursorCol = 1;
                } else if (ch == '\t') {
                    m_cursorCol = qMin(m_cols, ((m_cursorCol - 1) / 8 + 1) * 8 + 1);
                }
                start = j + 1;
            }
        }
        if (start < len) {
            QString textToInsert = cleanText.mid(start);
            writeTextSegment(textToInsert, token.format);
        }
        break;
    }
    case AnsiToken::ClearScreen:
    case AnsiToken::EraseInDisplay: {
        int mode = (token.type == AnsiToken::ClearScreen) ? 2 : token.val1;
        QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] EraseInDisplay"
                                         << "mode =" << mode
                                         << "before startRow =" << m_screenBufferStartRow
                                         << "cursor =" << m_cursorRow << m_cursorCol;
        if (mode == 2 || mode == 3) {
            clear();
            QTextDocument *doc = document();
            QTextCursor cursor(doc);
            QString initialBlank;
            for (int r = 1; r < m_rows; ++r) {
                initialBlank += "\n";
            }
            cursor.insertText(initialBlank);
            m_parser.reset();
            m_cursorRow = 1;
            m_cursorCol = 1;
            m_screenBufferStartRow = 0;
            QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] EraseInDisplay reset-screen";
        } else if (mode == 0) {
            QTextDocument *doc = document();
            int docRow = m_screenBufferStartRow + m_cursorRow - 1;
            if (doc->blockCount() > docRow) {
                QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
                QTextCursor writeCursor(block);
                int charIdx = blockColumnToCharIndex(block, m_cursorCol);
                writeCursor.setPosition(block.position() + charIdx);
                writeCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                writeCursor.removeSelectedText();
            }
            for (int r = m_cursorRow + 1; r <= m_rows; ++r) {
                int targetDocRow = m_screenBufferStartRow + r - 1;
                if (targetDocRow >= 0 && targetDocRow < doc->blockCount()) {
                    QTextBlock block = doc->findBlockByNumber(targetDocRow);
                    QTextCursor writeCursor(block);
                    writeCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                    writeCursor.removeSelectedText();
                }
            }
        } else if (mode == 1) {
            QTextDocument *doc = document();
            int docRow = m_screenBufferStartRow + m_cursorRow - 1;
            if (doc->blockCount() > docRow) {
                QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
                QTextCursor writeCursor(block);
                int charIdx = blockColumnToCharIndex(block, m_cursorCol);
                if (charIdx > 0) {
                    writeCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, charIdx);
                    writeCursor.removeSelectedText();
                    writeCursor.insertText(QString(charIdx, ' '), QTextCharFormat());
                }
            }
            for (int r = 1; r < m_cursorRow; ++r) {
                int targetDocRow = m_screenBufferStartRow + r - 1;
                if (targetDocRow >= 0 && targetDocRow < doc->blockCount()) {
                    QTextBlock block = doc->findBlockByNumber(targetDocRow);
                    QTextCursor writeCursor(block);
                    writeCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                    writeCursor.removeSelectedText();
                }
            }
        }
        break;
    }
    case AnsiToken::Backspace: {
        m_cursorCol = qMax(1, m_cursorCol - 1);
        break;
    }
    case AnsiToken::ClearLine:
    case AnsiToken::EraseInLine: {
        int mode = (token.type == AnsiToken::ClearLine) ? 2 : token.val1;
        QTextDocument *doc = document();
        int docRow = m_screenBufferStartRow + m_cursorRow - 1;
        if (doc->blockCount() > docRow) {
            QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
            QTextCursor writeCursor(block);
            if (mode == 2) {
                writeCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                writeCursor.removeSelectedText();
                m_cursorCol = 1;
            } else if (mode == 0) {
                int charIdx = blockColumnToCharIndex(block, m_cursorCol);
                writeCursor.setPosition(block.position() + charIdx);
                writeCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                writeCursor.removeSelectedText();
            } else if (mode == 1) {
                int charIdx = blockColumnToCharIndex(block, m_cursorCol);
                if (charIdx > 0) {
                    writeCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, charIdx);
                    writeCursor.removeSelectedText();
                    writeCursor.insertText(QString(charIdx, ' '), QTextCharFormat());
                }
            }
        }
        break;
    }
    case AnsiToken::DeleteCharacter: {
        QTextDocument *doc = document();
        int docRow = m_screenBufferStartRow + m_cursorRow - 1;
        if (doc->blockCount() > docRow) {
            QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
            int charIdx = blockColumnToCharIndex(block, m_cursorCol);
            if (charIdx < block.text().length()) {
                QTextCursor writeCursor(block);
                writeCursor.setPosition(block.position() + charIdx);
                int charsToRemove = 0;
                int removedWidth = 0;
                QString blockText = block.text();
                for (int j = charIdx; j < blockText.length(); ) {
                    int w = blockCharDisplayWidthAt(block, j);
                    int logicalLen = (j < blockText.length() - 1 && blockText.at(j).isHighSurrogate()) ? 2 : 1;
                    if (removedWidth + w > token.val1) {
                        break;
                    }
                    removedWidth += w;
                    charsToRemove += logicalLen;
                    j += logicalLen;
                }
                if (charsToRemove > 0) {
                    writeCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, charsToRemove);
                    writeCursor.removeSelectedText();
                }
            }
        }
        break;
    }
    case AnsiToken::EraseCharacter: {
        QTextDocument *doc = document();
        int docRow = m_screenBufferStartRow + m_cursorRow - 1;
        if (doc->blockCount() > docRow) {
            QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
            int currentWidth = blockDisplayWidth(block);
            QTextCursor writeCursor(block);
            if (currentWidth < m_cursorCol - 1) {
                writeCursor.movePosition(QTextCursor::EndOfBlock);
                writeCursor.insertText(QString(m_cursorCol - 1 - currentWidth, ' '), QTextCharFormat());
                currentWidth = blockDisplayWidth(block);
            }
            int charIdx = blockColumnToCharIndex(block, m_cursorCol);
            writeCursor.setPosition(block.position() + charIdx);
            
            int charsToRemove = 0;
            int removedWidth = 0;
            QString blockText = block.text();
            for (int j = charIdx; j < blockText.length(); ) {
                int w = blockCharDisplayWidthAt(block, j);
                int logicalLen = (j < blockText.length() - 1 && blockText.at(j).isHighSurrogate()) ? 2 : 1;
                if (removedWidth + w > token.val1) {
                    break;
                }
                removedWidth += w;
                charsToRemove += logicalLen;
                j += logicalLen;
            }
            if (charsToRemove > 0) {
                writeCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, charsToRemove);
                writeCursor.removeSelectedText();
            }
            writeCursor.insertText(QString(token.val1, ' '), QTextCharFormat());
        }
        break;
    }
    case AnsiToken::InsertLine: {
        int count = qBound(1, token.val1, m_rows - m_cursorRow + 1);
        QTextDocument *doc = document();
        int docRowStart = m_screenBufferStartRow + m_cursorRow - 1;
        int docRowEnd = m_screenBufferStartRow + m_rows - 1;

        // 1. 将 docRowStart 到 docRowEnd - count 之间的行内容，向下移动覆盖（必须从底端倒序遍历，防止覆盖尚未复制的数据）
        for (int r = docRowEnd; r >= docRowStart + count; --r) {
            QTextBlock prevBlock = doc->findBlockByNumber(r - count);
            QTextBlock currentBlock = doc->findBlockByNumber(r);
            if (prevBlock.isValid() && currentBlock.isValid()) {
                QTextCursor cur(currentBlock);
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                cur.removeSelectedText();
                cur.insertText(prevBlock.text());
            }
        }
        // 2. 清空新插入的 count 行
        for (int r = docRowStart; r < docRowStart + count; ++r) {
            QTextBlock currentBlock = doc->findBlockByNumber(r);
            if (currentBlock.isValid()) {
                QTextCursor cur(currentBlock);
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                cur.removeSelectedText();
            }
        }
        break;
    }
    case AnsiToken::DeleteLine: {
        int count = qBound(1, token.val1, m_rows - m_cursorRow + 1);
        QTextDocument *doc = document();
        int docRowStart = m_screenBufferStartRow + m_cursorRow - 1;
        int docRowEnd = m_screenBufferStartRow + m_rows - 1;

        // 1. 将 docRowStart + count 到 docRowEnd 之间的行内容，向上移动覆盖
        for (int r = docRowStart; r <= docRowEnd - count; ++r) {
            QTextBlock nextBlock = doc->findBlockByNumber(r + count);
            QTextBlock currentBlock = doc->findBlockByNumber(r);
            if (nextBlock.isValid() && currentBlock.isValid()) {
                QTextCursor cur(currentBlock);
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                cur.removeSelectedText();
                cur.insertText(nextBlock.text());
            }
        }
        // 2. 清空底部的 count 行
        for (int r = docRowEnd - count + 1; r <= docRowEnd; ++r) {
            QTextBlock currentBlock = doc->findBlockByNumber(r);
            if (currentBlock.isValid()) {
                QTextCursor cur(currentBlock);
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                cur.removeSelectedText();
            }
        }
        break;
    }
    case AnsiToken::HideCursor: {
        m_cursorVisible = false;
        checkHeuristicAlternateScreen();
        break;
    }
    case AnsiToken::ShowCursor: {
        m_cursorVisible = true;
        checkHeuristicAlternateScreen();
        break;
    }
    case AnsiToken::Win32InputModeSet: {
        m_win32InputModeActive = true;
        checkHeuristicAlternateScreen();
        break;
    }
    case AnsiToken::Win32InputModeReset: {
        m_win32InputModeActive = false;
        checkHeuristicAlternateScreen();
        break;
    }
    case AnsiToken::BracketedPasteSet: {
        m_bracketedPasteMode = true;
        break;
    }
    case AnsiToken::BracketedPasteReset: {
        m_bracketedPasteMode = false;
        break;
    }
    case AnsiToken::MouseTrackingSet: {
        m_mouseTrackingEnabled = true;
        m_mouseTrackingMode = token.val1;
        if (m_mouseTrackingMode == 1002 || m_mouseTrackingMode == 1003) {
            setMouseTracking(true);
        } else {
            setMouseTracking(false);
        }
        break;
    }
    case AnsiToken::MouseTrackingReset: {
        m_mouseTrackingEnabled = false;
        m_mouseTrackingMode = 0;
        setMouseTracking(false);
        break;
    }
    case AnsiToken::MouseEncodingSet: {
        m_mouseEncoding = token.val1;
        break;
    }
    case AnsiToken::MouseEncodingReset: {
        m_mouseEncoding = 0;
        break;
    }
    case AnsiToken::DeviceAttributesQuery: {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] handleToken: DEVICE ATTRIBUTES QUERY, replying xterm basic DA.";
        writeToPty("\033[?64;1;2;6;9;15;22c");
        break;
    }
    case AnsiToken::SecondaryDeviceAttributesQuery: {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] handleToken: SECONDARY DEVICE ATTRIBUTES QUERY, replying secondary DA.";
        writeToPty("\033[>0;115;0c");
        break;
    }
    }
}

void TerminalWidget::keyPressEvent(QKeyEvent *e)
{
    int key = e->key();
    QString text = e->text();
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] keyPressEvent: key =" << key << "text =" << text.toUtf8().toHex() << "modifiers =" << e->modifiers();

    if (!m_pty || !m_isShellRunning) {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] keyPressEvent: key IGNORED because shell is not running. m_pty =" << m_pty << "m_isShellRunning =" << m_isShellRunning;
        QPlainTextEdit::keyPressEvent(e);
        return;
    }

    // Shift+Ctrl+C/V for Copy/Paste inside terminal
    if ((e->modifiers() & Qt::ControlModifier) && (e->modifiers() & Qt::ShiftModifier)) {
        if (e->key() == Qt::Key_C) {
            copy();
            return;
        }
        if (e->key() == Qt::Key_V) {
            insertFromMimeData(QGuiApplication::clipboard()->mimeData());
            return;
        }
    }

    TranslatorState state;
    state.win32InputModeActive = m_win32InputModeActive;
    state.bracketedPasteActive = m_bracketedPasteMode;

    QByteArray data = InputTranslator::translateKeyEvent(e, state);
    if (!data.isEmpty()) {
        // 🌟 用户的键盘输入说明用户有交互意图，立即将跟随状态恢复为 true 并同步视口
        m_followTerminalOutput = true;
        syncCursor();

        writeToPty(data);
        e->accept();
    } else {
        // 如果在 win32 输入模式下，吃掉没有被转义出来的多余键盘事件，防止触发 QTextEdit 默认行为
        if (m_win32InputModeActive) {
            e->accept();
        } else {
            QPlainTextEdit::keyPressEvent(e);
        }
    }
}

void TerminalWidget::keyReleaseEvent(QKeyEvent *e)
{
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] keyReleaseEvent: key =" << e->key() << "modifiers =" << e->modifiers();

    if (!m_pty || !m_isShellRunning) {
        QPlainTextEdit::keyReleaseEvent(e);
        return;
    }

    if (m_win32InputModeActive) {
        TranslatorState state;
        state.win32InputModeActive = true;
        state.bracketedPasteActive = m_bracketedPasteMode;

        QByteArray data = InputTranslator::translateKeyEvent(e, state);
        if (!data.isEmpty()) {
            writeToPty(data);
        }
        e->accept();
    } else {
        QPlainTextEdit::keyReleaseEvent(e);
    }
}

void TerminalWidget::writeToPty(const QByteArray &data)
{
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] writeToPty: writing bytes =" << data.toHex();
    if (m_pty) {
        qint64 bytes = m_pty->write(data);
        QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] writeToPty: bytes written =" << bytes;
    } else {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] writeToPty: m_pty is NULL.";
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QFontMetrics fm(font());
    int charWidth = qMax(1, fm.horizontalAdvance('A'));
    int charHeight = qMax(1, fm.lineSpacing());
    int cols = viewport()->width() / charWidth;
    int rows = viewport()->height() / charHeight;

    if (cols < 40) cols = 40;
    if (rows < 10) rows = 10;

    QT_TERMINAL_TRACE_STREAM << "[TRACE] resizeEvent: viewport width =" << viewport()->width() << "height =" << viewport()->height()
                             << "cols =" << cols << "rows =" << rows << "pending shell path =" << m_pendingShellPath;

    // 1. 如果有延迟启动任务且 shell 还没运行，在这里拉起 Shell
    if (!m_pendingShellPath.isEmpty() && !m_isShellRunning) {
        // 只有物理尺寸足够合理时，才拉起 Shell
        if (viewport()->width() < 200 || viewport()->height() < 100) {
            QT_TERMINAL_TRACE_STREAM << "[TRACE] resizeEvent: size not ready for lazy start yet.";
            return;
        }
        QString path = m_pendingShellPath == "default" ? "" : m_pendingShellPath;
        m_pendingShellPath.clear();
        QT_TERMINAL_TRACE_STREAM << "[TRACE] resizeEvent: size ready, triggering Lazy Start shell.";
        startShell(path);
        return;
    }

    // 2. 如果 Shell 已经运行，进行动态伸缩和 resize
    if (m_pty && m_isShellRunning) {
        // 避免冗余 resize
        if (m_cols == cols && m_rows == rows) {
            return;
        }
        QT_TERMINAL_TRACE_STREAM << "[TRACE] resizeEvent: resizing active shell process to cols =" << cols << "rows =" << rows
                                 << "isAlternateBuffer =" << m_isAlternateBuffer;
        m_cols = cols;
        m_rows = rows;
        m_pty->resize(cols, rows);

        // 视口自适应对齐底端：若行数足够，将视口起始行向下推移，使视口精确覆盖文档缓冲区最末尾的 m_rows 行
        QTextDocument *doc = document();
        if (m_isAlternateBuffer) {
            m_screenBufferStartRow = 0;
            doc->clear();
            QTextCursor cursor(doc);
            QString initialBlank;
            for (int r = 1; r < m_rows; ++r) {
                initialBlank += "\n";
            }
            cursor.insertText(initialBlank);
            m_cursorRow = 1;
            m_cursorCol = 1;
            QT_TERMINAL_TRACE_STREAM << "[TRACE] resizeEvent (ALT): Cleared and re-initialized to m_rows =" << m_rows;
        } else {
            if (doc->blockCount() > m_rows) {
                m_screenBufferStartRow = doc->blockCount() - m_rows;
            } else {
                m_screenBufferStartRow = 0;
            }
            QT_TERMINAL_TRACE_STREAM << "[TRACE] resizeEvent (PRIMARY): blockCount =" << doc->blockCount() << "m_rows =" << m_rows
                                     << "m_screenBufferStartRow =" << m_screenBufferStartRow;
        }

        // 物理列宽截断：若宽度变窄，将每一行右侧超出新列宽 m_cols 边界的残留文本物理裁切，防止影响光标覆盖定位
        for (int r = 0; r < doc->blockCount(); ++r) {
            QTextBlock block = doc->findBlockByNumber(r);
            if (block.isValid()) {
                int currentW = blockDisplayWidth(block);
                if (currentW > m_cols) {
                    int charIdx = blockColumnToCharIndex(block, m_cols + 1);
                    if (charIdx < block.text().length()) {
                        QTextCursor cutCursor(block);
                        cutCursor.setPosition(block.position() + charIdx);
                        cutCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                        cutCursor.removeSelectedText();
                    }
                }
            }
        }

        // 强行收窄逻辑光标位置，防止越界
        m_cursorRow = qBound(1, m_cursorRow, m_rows);
        m_cursorCol = qBound(1, m_cursorCol, m_cols);

        syncCursor();
    }
}

void TerminalWidget::wheelEvent(QWheelEvent *e)
{
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] wheelEvent: delta =" << e->angleDelta() << "isAlternateBuffer =" << m_isAlternateBuffer
                                     << "mouseTracking =" << m_mouseTrackingEnabled << "trackingMode =" << m_mouseTrackingMode
                                     << "encoding =" << m_mouseEncoding;
             
    if (m_isAlternateBuffer) {
        int deltaY = e->angleDelta().y();
        if (!m_mouseTrackingEnabled) {
            QPlainTextEdit::wheelEvent(e);
            return;
        }

        if (deltaY != 0) {
            QFontMetrics fm(font());
            int charWidth = qMax(1, fm.horizontalAdvance('A'));
            int charHeight = qMax(1, fm.lineSpacing());
            int col = e->position().x() / charWidth + 1;
            int row = e->position().y() / charHeight + 1;
            col = qBound(1, col, m_cols);
            row = qBound(1, row, m_rows);
            
            int clicks = qAbs(deltaY) / 120;
            if (clicks < 1) clicks = 1;
            if (clicks > 3) clicks = 3;
            
            QByteArray seq;
            int button = (deltaY > 0) ? 64 : 65;
            if (m_mouseEncoding == 1006) {
                for (int k = 0; k < clicks; ++k) {
                    seq.append(QString("\033[<%1;%2;%3M").arg(button).arg(col).arg(row).toUtf8());
                }
            } else {
                for (int k = 0; k < clicks; ++k) {
                    seq.append("\033[M");
                    seq.append((char)(button + 32));
                    seq.append((char)(qMin(223, col) + 32));
                    seq.append((char)(qMin(223, row) + 32));
                }
            }
            QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] wheelEvent (MOUSE REPORT): Sending mouse scroll VT sequence to Pty, button:"
                                             << button << "col:" << col << "row:" << row << "hex:" << seq.toHex();
            writeToPty(seq);
        }
        e->accept();
    } else {
        QPlainTextEdit::wheelEvent(e);
    }
}

void TerminalWidget::insertFromMimeData(const QMimeData *source)
{
    if (m_pty && m_isShellRunning && source->hasText()) {
        // 🌟 用户的粘贴动作说明有交互输入，立即恢复跟随状态并同步视口
        m_followTerminalOutput = true;
        syncCursor();

        TranslatorState state;
        state.win32InputModeActive = m_win32InputModeActive;
        state.bracketedPasteActive = m_bracketedPasteMode;

        QByteArray data = InputTranslator::translatePasteEvent(source->text(), state);
        if (!data.isEmpty()) {
            writeToPty(data);
        }
    }
}

void TerminalWidget::syncCursor()
{
    QTextDocument *doc = document();
    int preservedScrollValue = verticalScrollBar()->value();
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[DEBUG] syncCursor -> blockCount:" << doc->blockCount()
                                     << "First text:" << doc->begin().text()
                                     << "startRow:" << m_screenBufferStartRow
                                     << "cursorRow:" << m_cursorRow;
    QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] syncCursor begin"
                                     << "follow =" << m_followTerminalOutput
                                     << "alternate =" << m_isAlternateBuffer
                                     << "scroll =" << preservedScrollValue
                                     << "maximum =" << verticalScrollBar()->maximum()
                                     << "startRow =" << m_screenBufferStartRow
                                     << "cursor =" << m_cursorRow << m_cursorCol
                                     << "blocks =" << doc->blockCount();

    // 🌟 在进入可能触发滚动条信号的任何编辑或光标操作之前，开启同步标志保护，防止 m_followTerminalOutput 被篡改
    m_syncingScrollBar++;

    // 保证行数恒等于 m_screenBufferStartRow + m_rows，防范意外溢出
    while (doc->blockCount() < m_screenBufferStartRow + m_rows) {
        QTextCursor endCursor(doc);
        endCursor.movePosition(QTextCursor::End);
        endCursor.insertText("\n");
    }
    
    int docRow = m_screenBufferStartRow + m_cursorRow - 1;

    // 🌟 在非跟随且非备用屏的“历史浏览”状态下，如果逻辑光标所在行超出了可视视口，
    // 将真实光标定位行限制在当前可视视口末尾，防止 setTextCursor 强行滚动拉走视口
    if (!m_isAlternateBuffer && !m_followTerminalOutput) {
        int visibleMin = verticalScrollBar()->value();
        int visibleMax = visibleMin + m_rows - 1;
        if (docRow < visibleMin || docRow > visibleMax) {
            docRow = qBound(0, visibleMax, doc->blockCount() - 1);
        }
    }

    QTextBlock block = doc->findBlockByNumber(qBound(0, docRow, doc->blockCount() - 1));
    QTextCursor finalCursor(block);
    int currentWidth = blockDisplayWidth(block);
    if (currentWidth < m_cursorCol - 1) {
        int spacesNeeded = (m_cursorCol - 1) - currentWidth;
        if (spacesNeeded > 0) {
            finalCursor.movePosition(QTextCursor::EndOfBlock);
            finalCursor.insertText(QString(spacesNeeded, ' '), QTextCharFormat());
        }
    }
    
    int charIdx = blockColumnToCharIndex(block, m_cursorCol);
    
    finalCursor.setPosition(block.position() + charIdx);
    setTextCursor(finalCursor);
    if (m_isAlternateBuffer || m_followTerminalOutput) {
        ensureCursorVisible();
    }

    // 彻底防范 Qt 在 setTextCursor 时重置光标属性，每次同步光标后均强制覆盖设置其可见性与宽度
    QPalette p = palette();
    if (m_cursorVisible) {
        setCursorWidth(1);
        p.setColor(QPalette::Text, QColor(220, 220, 220));
    } else {
        setCursorWidth(0);
        p.setColor(QPalette::Text, Qt::transparent);
    }
    setPalette(p);

    // 强制垂直滚动条精确对齐到逻辑起始行，防止微小行高测量溢出导致的视口虚假向上滚动
    if (m_isAlternateBuffer) {
        verticalScrollBar()->setVisible(false);
        verticalScrollBar()->setValue(0);
    } else if (m_followTerminalOutput) {
        if (m_screenBufferStartRow == 0) {
            verticalScrollBar()->setValue(0);
            QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] syncCursor decision = top-visible";
        } else {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] syncCursor decision = follow-maximum";
        }
    } else {
        verticalScrollBar()->setValue(preservedScrollValue);
        QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] syncCursor decision = preserve-history"
                                         << "preserved =" << preservedScrollValue;
    }

    // 🌟 全部恢复完成后，关闭同步保护
    m_syncingScrollBar--;
    QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] syncCursor end"
                                     << "scroll =" << verticalScrollBar()->value()
                                     << "maximum =" << verticalScrollBar()->maximum()
                                     << "follow =" << m_followTerminalOutput;
    traceViewportState("syncCursor-end");
}

void TerminalWidget::checkHeuristicAlternateScreen()
{
    if (m_isAlternateBuffer && !m_isHeuristicAlternateBuffer) {
        return;
    }

    bool hasChild = (m_pty && hasActiveChildProcess(m_pty->pid()));
    bool shouldBeInAlt = shouldUseHeuristicAlternateScreen(m_win32InputModeActive, m_cursorVisible, hasChild);

    if (shouldBeInAlt && !m_isAlternateBuffer) {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] checkHeuristicAlternateScreen: Triggering heuristic ENTER alternate buffer.";
        m_isHeuristicAlternateBuffer = true;
        
        m_primaryDoc = document();
        if (!m_alternateDoc) {
            m_alternateDoc = new QTextDocument(this);
            m_alternateDoc->setDocumentLayout(new QPlainTextDocumentLayout(m_alternateDoc));
            m_alternateDoc->setDefaultFont(font());
        }
        m_isAlternateBuffer = true;
        setDocument(m_alternateDoc);

        m_alternateDoc->clear();
        QTextCursor cursor(m_alternateDoc);
        QString initialBlank;
        for (int r = 1; r < m_rows; ++r) {
            initialBlank += "\n";
        }
        cursor.insertText(initialBlank);

        m_cursorRow = 1;
        m_cursorCol = 1;
        m_screenBufferStartRow = 0;

        verticalScrollBar()->setVisible(false);
        verticalScrollBar()->setRange(0, 0);
    }
    else if (!shouldBeInAlt && m_isAlternateBuffer && m_isHeuristicAlternateBuffer) {
        QT_TERMINAL_TRACE_STREAM << "[TRACE] checkHeuristicAlternateScreen: Triggering heuristic EXIT alternate buffer.";
        m_isHeuristicAlternateBuffer = false;
        m_isAlternateBuffer = false;

        setDocument(m_primaryDoc);
        verticalScrollBar()->setVisible(true);
        m_cursorRow = qBound(1, m_cursorRow, m_rows);
        m_cursorCol = qBound(1, m_cursorCol, m_cols);

        QTextDocument *doc = document();
        if (doc->blockCount() > m_rows) {
            m_screenBufferStartRow = doc->blockCount() - m_rows;
        } else {
            m_screenBufferStartRow = 0;
        }
        syncCursor();
    }
}

bool TerminalWidget::shouldUseHeuristicAlternateScreen(bool win32InputModeActive, bool cursorVisible, bool hasChild)
{
    Q_UNUSED(win32InputModeActive);
    Q_UNUSED(cursorVisible);
    Q_UNUSED(hasChild);
    return false;
}


void TerminalWidget::mousePressEvent(QMouseEvent *e)
{
    if (m_mouseTrackingEnabled) {
        MouseState state;
        state.trackingEnabled = m_mouseTrackingEnabled;
        state.trackingMode = m_mouseTrackingMode;
        state.encoding = m_mouseEncoding;

        QFontMetrics fm(font());
        int cellW = qMax(1, fm.horizontalAdvance('A'));
        int cellH = qMax(1, fm.lineSpacing());

        QByteArray data = InputTranslator::translateMouseEvent(e, state, m_cols, m_rows, cellW, cellH);
        if (!data.isEmpty()) {
            writeToPty(data);
        }
        e->accept();
    } else {
        QPlainTextEdit::mousePressEvent(e);
    }
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_mouseTrackingEnabled) {
        MouseState state;
        state.trackingEnabled = m_mouseTrackingEnabled;
        state.trackingMode = m_mouseTrackingMode;
        state.encoding = m_mouseEncoding;

        QFontMetrics fm(font());
        int cellW = qMax(1, fm.horizontalAdvance('A'));
        int cellH = qMax(1, fm.lineSpacing());

        QByteArray data = InputTranslator::translateMouseEvent(e, state, m_cols, m_rows, cellW, cellH);
        if (!data.isEmpty()) {
            writeToPty(data);
        }
        e->accept();
    } else {
        QPlainTextEdit::mouseReleaseEvent(e);
        
        // 🌟 只有在用户没有选中任何文本时（纯单击），才将光标同步回提示符
        // 如果有选中，则跳过同步以保留高亮选区供用户复制
        if (!textCursor().hasSelection()) {
            syncCursor();
        }
    }
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_mouseTrackingEnabled) {
        MouseState state;
        state.trackingEnabled = m_mouseTrackingEnabled;
        state.trackingMode = m_mouseTrackingMode;
        state.encoding = m_mouseEncoding;

        QFontMetrics fm(font());
        int cellW = qMax(1, fm.horizontalAdvance('A'));
        int cellH = qMax(1, fm.lineSpacing());

        QByteArray data = InputTranslator::translateMouseEvent(e, state, m_cols, m_rows, cellW, cellH);
        if (!data.isEmpty()) {
            writeToPty(data);
        }
        e->accept();
    } else {
        QPlainTextEdit::mouseMoveEvent(e);
    }
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *e)
{
    // 🌟 智能右键复制/粘贴模式
    if (textCursor().hasSelection()) {
        // 如果有选区，右击直接执行复制，并清除选区
        copy();
        
        QTextCursor cursor = textCursor();
        cursor.clearSelection();
        setTextCursor(cursor);
        syncCursor();
    } else {
        // 如果无选区，右击直接将剪贴板内容写入 PTY (粘贴)
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard) {
            insertFromMimeData(clipboard->mimeData());
        }
    }
    e->accept(); // 接受事件，阻止默认右键菜单弹出
}

int TerminalWidget::charDisplayWidth(QChar ch) const
{
    ushort u = ch.unicode();
    if (u < 0x20) {
        return 0;
    }
    
    // 1. 普通半角英文字符（ASCII 可打印字符）：绝对是 1 列
    if (u >= 0x20 && u <= 0x7E) {
        return 1;
    }
    
    if (ch.isSurrogate()) {
        return 1;
    }
    
    // 2. 动态依据当前字体下的物理像素宽度反推最符合呈现效果的逻辑列宽（四舍五入）
    // 彻底消除由于 Unicode 规范和 ConPTY 的东亚 Ambiguous 字符判定分歧导致的列宽对不齐
    QFontMetrics fm(font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int actualW = fm.horizontalAdvance(ch);
    if (actualW <= 0) {
        return 1;
    }
    
    int cols = (actualW + cellW / 2) / cellW;
    return qMax(1, cols);
}

int TerminalWidget::stringDisplayWidth(const QString &str) const
{
    if (str.isEmpty()) return 0;
    
    // 纯 ASCII 加速判断，完全绕过测量以保证 100% 绝对一致
    bool allAscii = true;
    for (int i = 0; i < str.length(); ++i) {
        ushort u = str.at(i).unicode();
        if (u < 0x20 || u > 0x7E) {
            allAscii = false;
            break;
        }
    }
    if (allAscii) {
        return str.length();
    }
    
    int width = 0;
    for (int i = 0; i < str.length(); ++i) {
        if (str.at(i).isHighSurrogate() && i + 1 < str.length()) {
            QString emoji = str.mid(i, 2);
            QFontMetrics fm(font());
            int charWidth = fm.horizontalAdvance('A');
            if (charWidth <= 0) charWidth = 8;
            int pixelWidth = fm.horizontalAdvance(emoji);
            width += qMax(1, (pixelWidth + charWidth / 2) / charWidth);
            i++; // Skip low surrogate
        } else {
            width += charDisplayWidth(str.at(i));
        }
    }
    return width;
}

int TerminalWidget::columnToCharIndex(const QString &text, int targetCol) const
{
    if (targetCol <= 1) return 0;
    
    // 纯 ASCII 快速精确定位
    bool allAscii = true;
    for (int i = 0; i < text.length(); ++i) {
        ushort u = text.at(i).unicode();
        if (u < 0x20 || u > 0x7E) {
            allAscii = false;
            break;
        }
    }
    if (allAscii) {
        return qMin(text.length(), targetCol - 1);
    }
    // 使用混合字宽逻辑测算进行定位
    QFontMetrics fm(font());
    int charWidth = fm.horizontalAdvance('A');
    if (charWidth <= 0) charWidth = 8;
    
    int col = 1;
    int i = 0;
    for (; i < text.length(); ++i) {
        if (col >= targetCol) {
            break;
        }
        
        int len = 1;
        int w = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < text.length()) {
            len = 2;
            QString emoji = text.mid(i, 2);
            int pixelWidth = fm.horizontalAdvance(emoji);
            w = qMax(1, (pixelWidth + charWidth / 2) / charWidth);
        } else {
            w = charDisplayWidth(text.at(i));
        }
        
        col += w;
        if (len == 2) {
            i++; // Skip low surrogate
        }
    }
    return i;
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent *e)
{
    QT_TERMINAL_VERBOSE_TRACE_STREAM << "[TRACE] inputMethodEvent: commitString =" << e->commitString()
                                     << "preeditString =" << e->preeditString();

    if (!e->commitString().isEmpty()) {
        clearPreedit(); // 提交前必须彻底清空临时预编辑文本，防止网格残留

        m_followTerminalOutput = true;
        syncCursor();

        writeToPty(e->commitString().toUtf8());
        e->accept();
    } else if (!e->preeditString().isEmpty()) {
        // 由终端网格接管预编辑字符串的绘制
        applyPreedit(e->preeditString());
        e->accept();
    } else {
        // 如果两者均为空，这可能是输入法重置或状态变更
        // 必须清空预编辑，并交由基类 QPlainTextEdit 处理以同步 Qt 内部 IME 状态，严禁静默 accept 吃掉事件
        clearPreedit();
        QPlainTextEdit::inputMethodEvent(e);
    }
}

int TerminalWidget::blockDisplayWidth(const QTextBlock &block) const
{
    if (!block.isValid()) return 0;
    
    QFontMetrics fm(font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int totalWidth = 0;
    
    QTextBlock::iterator it;
    for (it = block.begin(); !it.atEnd(); ++it) {
        QTextFragment fragment = it.fragment();
        if (fragment.isValid()) {
            QString text = fragment.text();
            QTextCharFormat format = fragment.charFormat();
            int stretch = format.fontStretch();
            if (stretch <= 0) stretch = 100;
            
            int len = text.length();
            int i = 0;
            while (i < len) {
                QChar ch = text.at(i);
                int actualW = 0;
                int logicalLen = 1;
                
                if (ch.isHighSurrogate() && i + 1 < len) {
                    QString emoji = text.mid(i, 2);
                    actualW = fm.horizontalAdvance(emoji);
                    logicalLen = 2;
                } else {
                    actualW = fm.horizontalAdvance(ch);
                }
                
                int scaledW = (actualW * stretch) / 100;
                int charCols = (scaledW + cellW / 2) / cellW;
                if (charCols < 1 && actualW > 0) charCols = 1;
                if (ch.unicode() < 0x20) charCols = 0;
                
                totalWidth += charCols;
                i += logicalLen;
            }
        }
    }
    return totalWidth;
}

int TerminalWidget::blockColumnToCharIndex(const QTextBlock &block, int targetCol) const
{
    if (targetCol <= 1 || !block.isValid()) return 0;
    
    QFontMetrics fm(font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int col = 1;
    int charIdx = 0;
    
    QTextBlock::iterator it;
    for (it = block.begin(); !it.atEnd(); ++it) {
        QTextFragment fragment = it.fragment();
        if (fragment.isValid()) {
            QString text = fragment.text();
            QTextCharFormat format = fragment.charFormat();
            int stretch = format.fontStretch();
            if (stretch <= 0) stretch = 100;
            
            int len = text.length();
            int i = 0;
            while (i < len) {
                if (col >= targetCol) {
                    return charIdx;
                }
                
                QChar ch = text.at(i);
                int actualW = 0;
                int logicalLen = 1;
                
                if (ch.isHighSurrogate() && i + 1 < len) {
                    QString emoji = text.mid(i, 2);
                    actualW = fm.horizontalAdvance(emoji);
                    logicalLen = 2;
                } else {
                    actualW = fm.horizontalAdvance(ch);
                }
                
                int scaledW = (actualW * stretch) / 100;
                int charCols = (scaledW + cellW / 2) / cellW;
                if (charCols < 1 && actualW > 0) charCols = 1;
                if (ch.unicode() < 0x20) charCols = 0;
                
                col += charCols;
                charIdx += logicalLen;
                i += logicalLen;
            }
        }
    }
    return charIdx;
}

int TerminalWidget::blockCharDisplayWidthAt(const QTextBlock &block, int charIndex) const
{
    if (!block.isValid() || charIndex < 0) return 1;
    
    QFontMetrics fm(font());
    int cellW = qMax(1, fm.horizontalAdvance('A'));
    int currentIdx = 0;
    
    QTextBlock::iterator it;
    for (it = block.begin(); !it.atEnd(); ++it) {
        QTextFragment fragment = it.fragment();
        if (fragment.isValid()) {
            QString text = fragment.text();
            QTextCharFormat format = fragment.charFormat();
            int stretch = format.fontStretch();
            if (stretch <= 0) stretch = 100;
            
            int len = text.length();
            int i = 0;
            while (i < len) {
                int logicalLen = 1;
                if (text.at(i).isHighSurrogate() && i + 1 < len) {
                    logicalLen = 2;
                }
                
                if (currentIdx == charIndex) {
                    int actualW = 0;
                    if (logicalLen == 2) {
                        actualW = fm.horizontalAdvance(text.mid(i, 2));
                    } else {
                        actualW = fm.horizontalAdvance(text.at(i));
                    }
                    int scaledW = (actualW * stretch) / 100;
                    int charCols = (scaledW + cellW / 2) / cellW;
                    if (charCols < 1 && actualW > 0) charCols = 1;
                    if (text.at(i).unicode() < 0x20) charCols = 0;
                    return charCols;
                }
                
                currentIdx += logicalLen;
                i += logicalLen;
            }
        }
    }
    return 1;
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle) {
        // 返回当前逻辑光标在视口中的精确物理位置矩形，以指导输入法候选框定位
        // 彻底解耦于富文本插入点，完美支持历史浏览、备用屏及宽字符下的等价定位
        QFontMetrics fm(font());
        int cellW = qMax(1, fm.horizontalAdvance('A'));
        int cellH = qMax(1, fm.lineSpacing());
        
        int x = (m_cursorCol - 1) * cellW;
        int y = (m_cursorRow - 1) * cellH;
        return QRect(x, y, cellW, cellH);
    }
    return QPlainTextEdit::inputMethodQuery(query);
}

void TerminalWidget::applyPreedit(const QString &preeditStr)
{
    if (preeditStr.isEmpty()) {
        clearPreedit();
        return;
    }

    // 开启同步保护，防止修改文档引发 valueChanged 误判跟随状态
    m_syncingScrollBar++;

    if (!m_inPreedit) {
        m_preeditAnchorRow = m_cursorRow;
        m_preeditAnchorCol = m_cursorCol;
        m_inPreedit = true;
    } else {
        // 先删除上一次在 anchor 处绘制的预编辑文本
        QTextDocument *doc = document();
        int anchorDocRow = m_screenBufferStartRow + m_preeditAnchorRow - 1;
        if (anchorDocRow >= 0 && anchorDocRow < doc->blockCount()) {
            QTextBlock block = doc->findBlockByNumber(anchorDocRow);
            if (block.isValid()) {
                int charIdx = blockColumnToCharIndex(block, m_preeditAnchorCol);
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + charIdx);
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, m_currentPreeditString.length());
                cursor.removeSelectedText();
            }
        }
    }

    // 插入新预编辑文本并渲染特定的虚线/下划线样式以区分正式文本
    QTextDocument *doc = document();
    int anchorDocRow = m_screenBufferStartRow + m_preeditAnchorRow - 1;
    if (anchorDocRow >= 0 && anchorDocRow < doc->blockCount()) {
        QTextBlock block = doc->findBlockByNumber(anchorDocRow);
        if (block.isValid()) {
            int charIdx = blockColumnToCharIndex(block, m_preeditAnchorCol);
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + charIdx);

            QTextCharFormat format;
            format.setUnderlineStyle(QTextCharFormat::DashUnderline);
            format.setForeground(QColor(100, 200, 255)); // 采用淡蓝色渲染
            cursor.insertText(preeditStr, format);
        }
    }

    m_currentPreeditString = preeditStr;

    // 计算预编辑内容的物理显示宽度，并将逻辑光标向右偏置定位在拼音串末尾
    int preeditWidth = stringDisplayWidth(preeditStr);
    m_cursorRow = m_preeditAnchorRow;
    m_cursorCol = m_preeditAnchorCol + preeditWidth;

    syncCursor();
    
    m_syncingScrollBar--;
}

void TerminalWidget::clearPreedit()
{
    if (m_inPreedit) {
        m_syncingScrollBar++;

        QTextDocument *doc = document();
        int anchorDocRow = m_screenBufferStartRow + m_preeditAnchorRow - 1;
        if (anchorDocRow >= 0 && anchorDocRow < doc->blockCount()) {
            QTextBlock block = doc->findBlockByNumber(anchorDocRow);
            if (block.isValid()) {
                int charIdx = blockColumnToCharIndex(block, m_preeditAnchorCol);
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + charIdx);
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, m_currentPreeditString.length());
                cursor.removeSelectedText();
            }
        }

        m_cursorRow = m_preeditAnchorRow;
        m_cursorCol = m_preeditAnchorCol;
        m_currentPreeditString.clear();
        m_inPreedit = false;

        syncCursor();

        m_syncingScrollBar--;
    }
}

void TerminalWidget::traceViewportState(const char *context) const
{
    QT_TERMINAL_VIEWPORT_TRACE_STREAM << "[VIEWPORT] state"
                                     << context
                                     << "follow =" << m_followTerminalOutput
                                     << "alternate =" << m_isAlternateBuffer
                                     << "startRow =" << m_screenBufferStartRow
                                     << "cursor =" << m_cursorRow << m_cursorCol
                                     << "rows =" << m_rows
                                     << "blocks =" << document()->blockCount()
                                     << "scroll =" << verticalScrollBar()->value()
                                     << "maximum =" << verticalScrollBar()->maximum();
}
