#ifndef INPUTTRANSLATOR_H
#define INPUTTRANSLATOR_H

#include <QByteArray>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QString>
#include "terminal_global.h"

struct TranslatorState
{
    bool win32InputModeActive = false;
    bool bracketedPasteActive = false;
};

struct MouseState
{
    bool trackingEnabled = false;
    int trackingMode = 0; // 0, 1000 (Normal), 1002 (Drag), 1003 (All Move)
    int encoding = 0;     // 0 (X10 default), 1005 (UTF-8), 1006 (SGR)
};

class QT_TERMINAL_WIDGET_EXPORT InputTranslator
{
public:
    // 将键盘事件翻译为发送至 PTY 的字节流
    static QByteArray translateKeyEvent(QKeyEvent *event, const TranslatorState &state);

    // 将粘贴文本翻译为发送至 PTY 的字节流（处理括号粘贴模式）
    static QByteArray translatePasteEvent(const QString &text, const TranslatorState &state);

    // 将输入法事件翻译为发送至 PTY 的字节流
    static QByteArray translateInputMethodEvent(QInputMethodEvent *event, const TranslatorState &state);

    // 将鼠标事件翻译为发送至 PTY 的字节流
    static QByteArray translateMouseEvent(QMouseEvent *event, const MouseState &state, int cols, int rows, int cellW, int cellH);

private:
    // 辅助：获取 Windows 虚拟键码
    static int getWindowsVirtualKeyCode(QKeyEvent *event);

    // 辅助：获取 Windows 控制键状态位图
    static int getWindowsControlKeyState(Qt::KeyboardModifiers modifiers);
};

#endif // INPUTTRANSLATOR_H
