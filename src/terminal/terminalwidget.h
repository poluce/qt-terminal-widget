#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include <QPlainTextEdit>
#include "pty/iptyprocess.h"
#include "ansiparser.h"

class TerminalWidget : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Start a shell process inside the terminal widget
    bool startShell(const QString &shellPath);
    void stopShell();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void insertFromMimeData(const QMimeData *source) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;

private slots:
    void onPtyReadyRead();

private:
    void setupUi();
    void sendControlKey(QKeyEvent *e);
    void writeToPty(const QByteArray &data);
    void handleToken(const AnsiToken &token);
    void writeTextSegment(const QString &textToInsert, const QTextCharFormat &format);
    void writeTextSegmentInternal(const QString &textToInsert, const QTextCharFormat &format);
    void writeSingleCharString(const QString &textToInsert, int logicalW, const QTextCharFormat &format);
    void syncCursor();
    int charDisplayWidth(QChar ch) const;
    int stringDisplayWidth(const QString &str) const;
    int columnToCharIndex(const QString &text, int targetCol) const;
    int blockDisplayWidth(const QTextBlock &block) const;
    int blockColumnToCharIndex(const QTextBlock &block, int targetCol) const;
    int blockCharDisplayWidthAt(const QTextBlock &block, int charIndex) const;

private:
    IPtyProcess *m_pty;
    AnsiParser m_parser;
    bool m_isShellRunning;
    bool m_cursorVisible;
    QString m_pendingShellPath;

    // Grid cursor properties (1-indexed)
    int m_cursorRow;
    int m_cursorCol;

    // Grid dimensions
    int m_cols;
    int m_rows;

    // Saved cursor position (for SCP/RCP)
    int m_savedRow;
    int m_savedCol;

    // 屏幕视口在文档缓冲区中的起始行号（0-indexed）
    int m_screenBufferStartRow;
};

#endif // TERMINALWIDGET_H
