#include "terminalwidget.h"
#include <QKeyEvent>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QScrollBar>
#include <QProcessEnvironment>
#include <QFontDatabase>
#include <QFile>
#include <QDebug>
#include <QMessageBox>
#include <QTextBlock>
#include "pty/ptyqt.h"

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
{
    setupUi();
}

TerminalWidget::~TerminalWidget()
{
    stopShell();
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
}

bool TerminalWidget::startShell(const QString &shellPath)
{
    qDebug() << "[TRACE] startShell called with path:" << shellPath
             << "viewport width:" << viewport()->width() << "height:" << viewport()->height();

    if (m_isShellRunning) {
        qDebug() << "[TRACE] startShell: shell is already running.";
        return false;
    }

    // 检测是否拥有合理的物理大小。若未准备就绪，开启延迟启动（Lazy Start）防止初始超小尺寸导致输出截断
    if (viewport()->width() < 200 || viewport()->height() < 100) {
        qDebug() << "[TRACE] startShell: pending startup due to small viewport size.";
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
    qDebug() << "[TRACE] startShell: resolved shell path =" << realShellPath;

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
    qDebug() << "[TRACE] startShell: starting process with cols =" << cols << "rows =" << rows;

    // Load standard environment
    QStringList env = QProcessEnvironment::systemEnvironment().toStringList();

    // Start process
    bool ok = m_pty->startProcess(realShellPath, env, cols, rows);
    if (!ok) {
        qWarning() << "[TRACE] startShell: m_pty->startProcess FAILED. Error:" << m_pty->lastError();
        delete m_pty;
        m_pty = nullptr;
        return false;
    }

    m_isShellRunning = true;
    qDebug() << "[TRACE] startShell: process started successfully. PID debug info:" << m_pty->dumpDebugInfo();

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

    // Connect readyRead signal
    connect(m_pty->notifier(), &QIODevice::readyRead, this, &TerminalWidget::onPtyReadyRead);
    qDebug() << "[TRACE] startShell: connected readyRead signal.";

    setFocus();
    return true;
}

void TerminalWidget::stopShell()
{
    if (m_pty) {
        m_pty->kill();
        delete m_pty;
        m_pty = nullptr;
    }
    m_isShellRunning = false;
}

void TerminalWidget::onPtyReadyRead()
{
    if (!m_pty) {
        qDebug() << "[TRACE] onPtyReadyRead called but m_pty is NULL.";
        return;
    }

    QByteArray data = m_pty->readAll();
    qDebug() << "[TRACE] onPtyReadyRead: read bytes =" << data.size() << "hex =" << data.toHex();
    if (data.isEmpty()) return;

    QList<AnsiToken> tokens = m_parser.parse(data);

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);

    for (const AnsiToken &token : tokens) {
        handleToken(token);
    }

    cursor.endEditBlock();
    syncCursor();
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
                // 屏幕视口向下滚动一行：最末尾插入新空行，视口偏移量递增
                QTextCursor endCursor(doc);
                endCursor.movePosition(QTextCursor::End);
                endCursor.insertText("\n");
                m_screenBufferStartRow++;
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
    case AnsiToken::CursorPosition: {
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
                        // 不在头部物理删行，直接在文档最末尾追加新行，并且视口下移一行
                        QTextCursor endCursor(doc);
                        endCursor.movePosition(QTextCursor::End);
                        endCursor.insertText("\n");
                        m_screenBufferStartRow++;
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
        break;
    }
    case AnsiToken::ShowCursor: {
        m_cursorVisible = true;
        break;
    }
    }
}

void TerminalWidget::keyPressEvent(QKeyEvent *e)
{
    int key = e->key();
    QString text = e->text();
    qDebug() << "[TRACE] keyPressEvent: key =" << key << "text =" << text.toUtf8().toHex() << "modifiers =" << e->modifiers();

    if (!m_pty || !m_isShellRunning) {
        qDebug() << "[TRACE] keyPressEvent: key IGNORED because shell is not running. m_pty =" << m_pty << "m_isShellRunning =" << m_isShellRunning;
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

    // Check for Control keys (Ctrl + Key)
    if ((e->modifiers() & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        char ctrlChar = key - Qt::Key_A + 1;
        writeToPty(QByteArray(1, ctrlChar));
        return;
    }

    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
#ifdef Q_OS_WIN
        writeToPty("\r\n");
#else
        writeToPty("\r");
#endif
        break;
    case Qt::Key_Backspace:
        writeToPty("\x7f"); // Standard backspace code for ConPTY
        break;
    case Qt::Key_Tab:
        writeToPty("\t");
        break;
    case Qt::Key_Escape:
        writeToPty("\033");
        break;
    case Qt::Key_Up:
        writeToPty("\033[A");
        break;
    case Qt::Key_Down:
        writeToPty("\033[B");
        break;
    case Qt::Key_Right:
        writeToPty("\033[C");
        break;
    case Qt::Key_Left:
        writeToPty("\033[D");
        break;
    case Qt::Key_Home:
        writeToPty("\033[H");
        break;
    case Qt::Key_End:
        writeToPty("\033[F");
        break;
    case Qt::Key_Delete:
        writeToPty("\033[3~");
        break;
    case Qt::Key_PageUp:
        writeToPty("\033[5~");
        break;
    case Qt::Key_PageDown:
        writeToPty("\033[6~");
        break;
    default:
        if (!text.isEmpty()) {
            writeToPty(text.toUtf8());
        }
        break;
    }
}

void TerminalWidget::writeToPty(const QByteArray &data)
{
    qDebug() << "[TRACE] writeToPty: writing bytes =" << data.toHex();
    if (m_pty) {
        qint64 bytes = m_pty->write(data);
        qDebug() << "[TRACE] writeToPty: bytes written =" << bytes;
    } else {
        qDebug() << "[TRACE] writeToPty: m_pty is NULL.";
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

    qDebug() << "[TRACE] resizeEvent: viewport width =" << viewport()->width() << "height =" << viewport()->height()
             << "cols =" << cols << "rows =" << rows << "pending shell path =" << m_pendingShellPath;

    // 1. 如果有延迟启动任务且 shell 还没运行，在这里拉起 Shell
    if (!m_pendingShellPath.isEmpty() && !m_isShellRunning) {
        // 只有物理尺寸足够合理时，才拉起 Shell
        if (viewport()->width() < 200 || viewport()->height() < 100) {
            qDebug() << "[TRACE] resizeEvent: size not ready for lazy start yet.";
            return;
        }
        QString path = m_pendingShellPath == "default" ? "" : m_pendingShellPath;
        m_pendingShellPath.clear();
        qDebug() << "[TRACE] resizeEvent: size ready, triggering Lazy Start shell.";
        startShell(path);
        return;
    }

    // 2. 如果 Shell 已经运行，进行动态伸缩和 resize
    if (m_pty && m_isShellRunning) {
        // 避免冗余 resize
        if (m_cols == cols && m_rows == rows) {
            return;
        }
        qDebug() << "[TRACE] resizeEvent: resizing active shell process to cols =" << cols << "rows =" << rows;
        m_cols = cols;
        m_rows = rows;
        m_pty->resize(cols, rows);

        // 视口自适应对齐底端：若行数足够，将视口起始行向下推移，使视口精确覆盖文档缓冲区最末尾的 m_rows 行
        QTextDocument *doc = document();
        if (doc->blockCount() > m_rows) {
            m_screenBufferStartRow = doc->blockCount() - m_rows;
        } else {
            m_screenBufferStartRow = 0;
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

void TerminalWidget::insertFromMimeData(const QMimeData *source)
{
    if (m_pty && m_isShellRunning && source->hasText()) {
        writeToPty(source->text().toUtf8());
    }
}

void TerminalWidget::syncCursor()
{
    QTextDocument *doc = document();
    qDebug() << "[DEBUG] syncCursor -> blockCount:" << doc->blockCount()
             << "First text:" << doc->begin().text()
             << "startRow:" << m_screenBufferStartRow
             << "cursorRow:" << m_cursorRow;
    // 保证行数恒等于 m_screenBufferStartRow + m_rows，防范意外溢出
    while (doc->blockCount() < m_screenBufferStartRow + m_rows) {
        QTextCursor endCursor(doc);
        endCursor.movePosition(QTextCursor::End);
        endCursor.insertText("\n");
    }
    
    int docRow = m_screenBufferStartRow + m_cursorRow - 1;
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
    ensureCursorVisible();

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
    verticalScrollBar()->setValue(m_screenBufferStartRow);
}

void TerminalWidget::mousePressEvent(QMouseEvent *e)
{
    QPlainTextEdit::mousePressEvent(e);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseReleaseEvent(e);
    
    // 🌟 只有在用户没有选中任何文本时（纯单击），才将光标同步回提示符
    // 如果有选中，则跳过同步以保留高亮选区供用户复制
    if (!textCursor().hasSelection()) {
        syncCursor();
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
    qDebug() << "[TRACE] inputMethodEvent: commitString =" << e->commitString()
             << "preeditString =" << e->preeditString();

    if (!e->commitString().isEmpty()) {
        writeToPty(e->commitString().toUtf8());
    }
    e->accept();
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
