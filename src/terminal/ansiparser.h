#ifndef ANSIPARSER_H
#define ANSIPARSER_H

#include <QString>
#include <QList>
#include <QTextCharFormat>
#include <QColor>
#include <QStringDecoder>

struct AnsiToken
{
    enum Type {
        Text,
        ClearScreen,
        ClearLine,
        CursorPosition,
        Backspace,

        // 新增控制类型
        CursorUp,                 // CUU (A)
        CursorDown,               // CUD (B)
        CursorForward,            // CUF (C)
        CursorBackward,           // CUB (D)
        CursorNextLine,           // CNL (E)
        CursorPrevLine,           // CPL (F)
        CursorHorizontalAbsolute, // CHA (G)

        SaveCursor,               // SCP (s)
        RestoreCursor,            // RCP (u)

        EraseInDisplay,           // ED (J) - val1 包含模式 (0, 1, 2, 3)
        EraseInLine,              // EL (K) - val1 包含模式 (0, 1, 2)

        DeleteCharacter,          // DCH (P) - val1 包含个数
        EraseCharacter,           // ECH (X) - val1 包含个数
        InsertLine,               // IL (L) - val1 包含个数
        DeleteLine,                // DL (M) - val1 包含个数
        HideCursor,
        ShowCursor,
        EnterAlternateBuffer,
        ExitAlternateBuffer,
        Win32InputModeSet,
        Win32InputModeReset,
        MouseTrackingSet,
        MouseTrackingReset,
        DeviceAttributesQuery,
        SecondaryDeviceAttributesQuery
    };
    Type type;
    QString text;
    QTextCharFormat format;
    int cursorRow = -1;
    int cursorCol = -1;
    int val1 = -1;
    int val2 = -1;
};

class AnsiParser
{
public:
    enum class State {
        Ground,       // Normal text output
        Escape,       // ESC (\033) received
        EscapeIntermediate, // Skipping intermediate character-set designator
        CsiEntry,     // CSI (ESC [) sequence parameter collection
        OscString     // OSC (ESC ]) sequence command collection (e.g. window title)
    };

    AnsiParser();

    QList<AnsiToken> parse(const QByteArray &data);
    void reset();

private:
    void parseEscapeSequence(const QByteArray &seq, char cmd, QList<AnsiToken> &tokens);
    QColor getAnsiColor(int code, bool bright);
    QColor get256Color(int index);

private:
    QTextCharFormat m_currentFormat;
    bool m_bold;
    bool m_inverted;
    QColor m_fgColor;
    QColor m_bgColor;
    bool m_fgSet;
    bool m_bgSet;

    // State machine properties
    State m_state;
    QByteArray m_sequenceBuffer;
    QStringDecoder m_decoder;
};

#endif // ANSIPARSER_H
