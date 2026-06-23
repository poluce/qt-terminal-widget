#include "inputtranslator.h"
#include <QDebug>

QByteArray InputTranslator::translateKeyEvent(QKeyEvent *event, const TranslatorState &state)
{
    int key = event->key();
    QString text = event->text();

    if (state.win32InputModeActive) {
        // Win32 Input Mode High-Precision Input Report
        int keyDown = (event->type() == QEvent::KeyPress) ? 1 : 0;
        int repeatCount = 1;
        int virtualKeyCode = getWindowsVirtualKeyCode(event);
        int virtualScanCode = event->nativeScanCode();
        if (virtualScanCode < 0) virtualScanCode = 0;

        int unicodeChar = 0;
        // 如果是修饰键本身，Unicode 字符为 0
        if (key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Alt &&
            key != Qt::Key_Meta && key != Qt::Key_CapsLock && key != Qt::Key_NumLock &&
            key != Qt::Key_ScrollLock)
        {
            if (!text.isEmpty()) {
                unicodeChar = text.at(0).unicode();
            }
        }

        int controlKeyState = getWindowsControlKeyState(event->modifiers());

        // 微软规范格式: ESC [ Vk ; Sc ; Uc ; Kd ; Cs ; Rc _
        QByteArray seq = QString("\033[%1;%2;%3;%4;%5;%6_")
                             .arg(virtualKeyCode)
                             .arg(virtualScanCode)
                             .arg(unicodeChar)
                             .arg(keyDown)
                             .arg(controlKeyState)
                             .arg(repeatCount)
                             .toUtf8();
        return seq;
    }

    // 非 win32-input-mode 模式，松开按键不作处理
    if (event->type() == QEvent::KeyRelease) {
        return QByteArray();
    }

    // Ctrl + A-Z 快捷键处理
    if ((event->modifiers() & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        char ctrlChar = key - Qt::Key_A + 1;
        return QByteArray(1, ctrlChar);
    }
    
    // 其它 Ctrl 辅助快捷键
    if (event->modifiers() & Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_Space:
            return QByteArray(1, '\x00');
        case Qt::Key_Backslash:
            return QByteArray(1, '\x1c');
        case Qt::Key_BracketRight:
            return QByteArray(1, '\x1d');
        case Qt::Key_AsciiCircum:
            return QByteArray(1, '\x1e');
        case Qt::Key_Underscore:
            return QByteArray(1, '\x1f');
        case Qt::Key_Backspace:
            return QByteArray(1, '\x08');
        default:
            break;
        }
    }

    // Alt/Meta 修饰符前缀处理 (Alt-prefixing)
    bool hasAlt = (event->modifiers() & Qt::AltModifier);

    QByteArray baseData;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (hasAlt) {
            baseData = "\r";
        } else {
#ifdef Q_OS_WIN
            baseData = "\r\n";
#else
            baseData = "\r";
#endif
        }
        break;
    case Qt::Key_Backspace:
        baseData = "\x7f";
        break;
    case Qt::Key_Tab:
        baseData = "\t";
        break;
    case Qt::Key_Escape:
        baseData = "\033";
        break;
    case Qt::Key_Up:
        baseData = "\033[A";
        break;
    case Qt::Key_Down:
        baseData = "\033[B";
        break;
    case Qt::Key_Right:
        baseData = "\033[C";
        break;
    case Qt::Key_Left:
        baseData = "\033[D";
        break;
    case Qt::Key_Home:
        baseData = "\033[H";
        break;
    case Qt::Key_End:
        baseData = "\033[F";
        break;
    case Qt::Key_Delete:
        baseData = "\033[3~";
        break;
    case Qt::Key_PageUp:
        baseData = "\033[5~";
        break;
    case Qt::Key_PageDown:
        baseData = "\033[6~";
        break;
    default:
        if (!text.isEmpty()) {
            baseData = text.toUtf8();
        }
        break;
    }

    if (hasAlt && !baseData.isEmpty()) {
        return "\033" + baseData;
    }
    return baseData;
}

QByteArray InputTranslator::translatePasteEvent(const QString &text, const TranslatorState &state)
{
    if (text.isEmpty()) {
        return QByteArray();
    }

    if (state.bracketedPasteActive) {
        // Bracketed Paste Mode: Wrap text in ESC [ 200 ~ and ESC [ 201 ~
        return "\033[200~" + text.toUtf8() + "\033[201~";
    }

    return text.toUtf8();
}

QByteArray InputTranslator::translateInputMethodEvent(QInputMethodEvent *event, const TranslatorState &state)
{
    Q_UNUSED(state);
    if (!event->commitString().isEmpty()) {
        return event->commitString().toUtf8();
    }
    return QByteArray();
}

int InputTranslator::getWindowsVirtualKeyCode(QKeyEvent *event)
{
#ifdef Q_OS_WIN
    int nativeVK = event->nativeVirtualKey();
    if (nativeVK > 0) {
        return nativeVK;
    }
#endif

    int key = event->key();
    // 字母和数字直接映射
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return key;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return key;
    }

    // 后备特殊键到 Windows VK Code 的转换映射
    switch (key) {
    case Qt::Key_Left:      return 0x25; // VK_LEFT
    case Qt::Key_Up:        return 0x26; // VK_UP
    case Qt::Key_Right:     return 0x27; // VK_RIGHT
    case Qt::Key_Down:      return 0x28; // VK_DOWN
    case Qt::Key_Escape:    return 0x1B; // VK_ESCAPE
    case Qt::Key_Tab:       return 0x09; // VK_TAB
    case Qt::Key_Backtab:   return 0x09;
    case Qt::Key_Backspace: return 0x08; // VK_BACK
    case Qt::Key_Return:    return 0x0D; // VK_RETURN
    case Qt::Key_Enter:     return 0x0D;
    case Qt::Key_Insert:    return 0x2D; // VK_INSERT
    case Qt::Key_Delete:    return 0x2E; // VK_DELETE
    case Qt::Key_Home:      return 0x24; // VK_HOME
    case Qt::Key_End:       return 0x23; // VK_END
    case Qt::Key_PageUp:    return 0x21; // VK_PRIOR
    case Qt::Key_PageDown:  return 0x22; // VK_NEXT
    case Qt::Key_Space:     return 0x20; // VK_SPACE
    case Qt::Key_Shift:     return 0x10; // VK_SHIFT
    case Qt::Key_Control:   return 0x11; // VK_CONTROL
    case Qt::Key_Alt:       return 0x12; // VK_MENU
    case Qt::Key_CapsLock:  return 0x14; // VK_CAPITAL
    case Qt::Key_NumLock:   return 0x90; // VK_NUMLOCK
    case Qt::Key_ScrollLock:return 0x91; // VK_SCROLL
    case Qt::Key_F1:        return 0x70; // VK_F1
    case Qt::Key_F2:        return 0x71;
    case Qt::Key_F3:        return 0x72;
    case Qt::Key_F4:        return 0x73;
    case Qt::Key_F5:        return 0x74;
    case Qt::Key_F6:        return 0x75;
    case Qt::Key_F7:        return 0x76;
    case Qt::Key_F8:        return 0x77;
    case Qt::Key_F9:        return 0x78;
    case Qt::Key_F10:       return 0x79;
    case Qt::Key_F11:       return 0x7A;
    case Qt::Key_F12:       return 0x7B;
    case Qt::Key_Semicolon: return 0xBA; // VK_OEM_1
    case Qt::Key_Slash:     return 0xBF; // VK_OEM_2
    case Qt::Key_QuoteLeft: return 0xC0; // VK_OEM_3
    case Qt::Key_BracketLeft: return 0xDB; // VK_OEM_4
    case Qt::Key_Backslash: return 0xDC; // VK_OEM_5
    case Qt::Key_BracketRight: return 0xDD; // VK_OEM_6
    case Qt::Key_Apostrophe: return 0xDE; // VK_OEM_7
    case Qt::Key_Equal:     return 0xBB; // VK_OEM_PLUS
    case Qt::Key_Comma:     return 0xBC; // VK_OEM_COMMA
    case Qt::Key_Minus:     return 0xBD; // VK_OEM_MINUS
    case Qt::Key_Period:    return 0xBE; // VK_OEM_PERIOD
    default:
        break;
    }
    return 0;
}

int InputTranslator::getWindowsControlKeyState(Qt::KeyboardModifiers modifiers)
{
    int state = 0;
    if (modifiers & Qt::ShiftModifier) {
        state |= 0x0010; // SHIFT_PRESSED
    }
    if (modifiers & Qt::ControlModifier) {
        // 后续如果能在平台层精确区分，可以加入 RIGHT_CTRL_PRESSED
        state |= 0x0008; // LEFT_CTRL_PRESSED
    }
    if (modifiers & Qt::AltModifier) {
        state |= 0x0002; // LEFT_ALT_PRESSED
    }
    return state;
}

QByteArray InputTranslator::translateMouseEvent(QMouseEvent *event, const MouseState &state, int cols, int rows, int cellW, int cellH)
{
    if (!state.trackingEnabled) {
        return QByteArray();
    }

    int col = qBound(1, (int)(event->position().x() / cellW + 1), cols);
    int row = qBound(1, (int)(event->position().y() / cellH + 1), rows);

    int btn = 0;
    QEvent::Type type = event->type();

    if (type == QEvent::MouseButtonPress) {
        if (event->button() == Qt::LeftButton) {
            btn = 0;
        } else if (event->button() == Qt::MiddleButton) {
            btn = 1;
        } else if (event->button() == Qt::RightButton) {
            btn = 2;
        } else {
            return QByteArray();
        }
    } else if (type == QEvent::MouseButtonRelease) {
        btn = 3; // Release is always 3
    } else if (type == QEvent::MouseMove) {
        // 模式 1000 只追踪按下和松开，不报告移动
        if (state.trackingMode == 1000) {
            return QByteArray();
        }

        bool hasButtons = (event->buttons() & (Qt::LeftButton | Qt::MiddleButton | Qt::RightButton));
        // 模式 1002 报告拖拽事件（即有按键按下时的移动）
        if (state.trackingMode == 1002 && !hasButtons) {
            return QByteArray();
        }

        if (event->buttons() & Qt::LeftButton) {
            btn = 32;
        } else if (event->buttons() & Qt::MiddleButton) {
            btn = 33;
        } else if (event->buttons() & Qt::RightButton) {
            btn = 34;
        } else {
            btn = 35; // 模式 1003 报告无按键按下的普通移动
        }
    } else {
        return QByteArray();
    }

    // 应用修饰键偏置
    if (event->modifiers() & Qt::ShiftModifier) btn |= 4;
    if (event->modifiers() & Qt::AltModifier) btn |= 8;
    if (event->modifiers() & Qt::ControlModifier) btn |= 16;

    if (state.encoding == 1006) {
        // SGR 1006 Protocol: ESC [ < Button ; Col ; Row M/m
        char releaseChar = (type == QEvent::MouseButtonRelease) ? 'm' : 'M';
        return QString("\033[<%1;%2;%3%4").arg(btn).arg(col).arg(row).arg(releaseChar).toUtf8();
    } else if (state.encoding == 1005) {
        // UTF-8 1005 Protocol: C_b, C_x, C_y mapped as Unicode code points in UTF-8
        QByteArray seq;
        seq.append("\033[M");
        QString coords;
        coords.append(QChar(btn + 32));
        coords.append(QChar(col + 32));
        coords.append(QChar(row + 32));
        seq.append(coords.toUtf8());
        return seq;
    } else {
        // Standard X10 Protocol
        QByteArray seq;
        seq.append("\033[M");
        seq.append((char)(btn + 32));
        seq.append((char)(qMin(223, col) + 32));
        seq.append((char)(qMin(223, row) + 32));
        return seq;
    }
}

