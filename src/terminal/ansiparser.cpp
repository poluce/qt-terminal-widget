#include "ansiparser.h"
#include <QStringList>
#include <QDebug>

AnsiParser::AnsiParser()
{
    reset();
}

void AnsiParser::reset()
{
    m_bold = false;
    m_inverted = false;
    m_fgSet = false;
    m_bgSet = false;
    m_fgColor = QColor();
    m_bgColor = QColor();
    m_currentFormat = QTextCharFormat();
    m_state = State::Ground;
    m_sequenceBuffer.clear();
    m_decoder = QStringDecoder(QStringDecoder::Utf8);
}

QList<AnsiToken> AnsiParser::parse(const QByteArray &data)
{
    QList<AnsiToken> tokens;
    QByteArray textBytes;

    for (int i = 0; i < data.size(); ++i) {
        char ch = data.at(i);

        // Anywhere rule: ESC (0x1b) always transitions to Escape state,
        // committing any pending text block and resetting the parser state.
        if (ch == '\033') {
            if (m_state == State::Ground && !textBytes.isEmpty()) {
                AnsiToken token;
                token.type = AnsiToken::Text;
                token.text = m_decoder(textBytes);
                token.format = m_currentFormat;
                tokens.append(token);
                textBytes.clear();
            }
            m_state = State::Escape;
            m_sequenceBuffer.clear();
            continue;
        }

        switch (m_state) {
        case State::Ground: {
            if (ch == '\b') {
                if (!textBytes.isEmpty()) {
                    AnsiToken token;
                    token.type = AnsiToken::Text;
                    token.text = m_decoder(textBytes);
                    token.format = m_currentFormat;
                    tokens.append(token);
                    textBytes.clear();
                }
                AnsiToken token;
                token.type = AnsiToken::Backspace;
                tokens.append(token);
            } else {
                textBytes.append(ch);
            }
            break;
        }
        case State::Escape: {
            if (ch == '[') {
                m_state = State::CsiEntry;
                m_sequenceBuffer.clear();
            } else if (ch == ']') {
                m_state = State::OscString;
                m_sequenceBuffer.clear();
            } else if (ch == '(' || ch == ')') {
                // Character set escape sequence, skip the next byte
                m_state = State::EscapeIntermediate;
            } else {
                // Consume character and return to Ground
                m_state = State::Ground;
            }
            break;
        }
        case State::EscapeIntermediate: {
            // Consume the intermediate symbol and return to Ground
            m_state = State::Ground;
            break;
        }
        case State::CsiEntry: {
            m_sequenceBuffer.append(ch);
            // CSI parameter ranges terminate on a letter in 0x40 - 0x7E range
            if (ch >= 0x40 && ch <= 0x7E) {
                if (!m_sequenceBuffer.isEmpty()) {
                    char cmd = m_sequenceBuffer.at(m_sequenceBuffer.size() - 1);
                    QByteArray seq = m_sequenceBuffer.left(m_sequenceBuffer.size() - 1);
                    parseEscapeSequence(seq, cmd, tokens);
                }
                m_sequenceBuffer.clear();
                m_state = State::Ground;
            }
            break;
        }
        case State::OscString: {
            m_sequenceBuffer.append(ch);
            // OSC sequence ends on BEL (\x07)
            if (ch == '\x07') {
                m_sequenceBuffer.clear();
                m_state = State::Ground;
            }
            break;
        }
        }
    }

    // Process trailing text in ground state
    if (!textBytes.isEmpty() && m_state == State::Ground) {
        AnsiToken token;
        token.type = AnsiToken::Text;
        token.text = m_decoder(textBytes);
        token.format = m_currentFormat;
        tokens.append(token);
    }

    return tokens;
}

void AnsiParser::parseEscapeSequence(const QByteArray &seq, char cmd, QList<AnsiToken> &tokens)
{
    QString seqStr = QString::fromLatin1(seq);
    QStringList params = seqStr.split(';');

    switch (cmd) {
    case 'm': { // SGR (Select Graphic Rendition)
        int k = 0;
        int numParams = params.size();
        while (k < numParams) {
            QString param = params.at(k);
            if (param.isEmpty()) {
                m_bold = false;
                m_fgSet = false;
                m_bgSet = false;
                m_inverted = false;
                m_currentFormat = QTextCharFormat();
                k++;
                continue;
            }

            bool ok;
            int code = param.toInt(&ok);
            if (!ok) {
                qWarning() << "[AnsiParser] SGR param conversion failed for:" << param;
                k++;
                continue;
            }

            if (code == 0) {
                m_bold = false;
                m_fgSet = false;
                m_bgSet = false;
                m_inverted = false;
                m_currentFormat = QTextCharFormat();
                k++;
            } else if (code == 1) {
                m_bold = true;
                m_currentFormat.setFontWeight(QFont::Bold);
                k++;
            } else if (code == 22) {
                m_bold = false;
                m_currentFormat.setFontWeight(QFont::Normal);
                k++;
            } else if (code == 7) {
                m_inverted = true;
                k++;
            } else if (code == 27) {
                m_inverted = false;
                k++;
            } else if (code == 38 || code == 48) {
                // 38 -> Foreground color; 48 -> Background color
                if (k + 1 < numParams) {
                    QString type = params.at(k + 1);
                    if (type == "5" && k + 2 < numParams) {
                        // 256-color palette
                        int colorIdx = params.at(k + 2).toInt();
                        QColor color = get256Color(colorIdx);
                        if (code == 38) {
                            m_fgColor = color;
                            m_fgSet = true;
                        } else {
                            m_bgColor = color;
                            m_bgSet = true;
                        }
                        k += 3;
                    } else if (type == "2" && k + 4 < numParams) {
                        // 24-bit TrueColor RGB
                        int r = params.at(k + 2).toInt();
                        int g = params.at(k + 3).toInt();
                        int b = params.at(k + 4).toInt();
                        QColor color(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
                        if (code == 38) {
                            m_fgColor = color;
                            m_fgSet = true;
                        } else {
                            m_bgColor = color;
                            m_bgSet = true;
                        }
                        k += 5;
                    } else {
                        k++;
                    }
                } else {
                    k++;
                }
            } else if (code >= 30 && code <= 37) {
                m_fgColor = getAnsiColor(code - 30, m_bold);
                m_fgSet = true;
                k++;
            } else if (code == 39) {
                m_fgSet = false;
                k++;
            } else if (code >= 40 && code <= 47) {
                m_bgColor = getAnsiColor(code - 40, m_bold);
                m_bgSet = true;
                k++;
            } else if (code == 49) {
                m_bgSet = false;
                k++;
            } else if (code >= 90 && code <= 97) {
                m_fgColor = getAnsiColor(code - 90, true);
                m_fgSet = true;
                k++;
            } else if (code >= 100 && code <= 107) {
                m_bgColor = getAnsiColor(code - 100, true);
                m_bgSet = true;
                k++;
            } else {
                k++;
            }
        }

        // 统一在循环结束后根据 m_inverted 状态应用前景色和背景色
        QColor defaultFg = QColor(220, 220, 220);
        QColor defaultBg = QColor(20, 20, 20); // 黑色底色
        
        QColor activeFg = m_fgSet ? m_fgColor : defaultFg;
        QColor activeBg = m_bgSet ? m_bgColor : defaultBg;
        
        if (m_inverted) {
            m_currentFormat.setForeground(activeBg);
            m_currentFormat.setBackground(activeFg);
        } else {
            m_currentFormat.setForeground(activeFg);
            if (m_bgSet) {
                m_currentFormat.setBackground(activeBg);
            } else {
                m_currentFormat.clearBackground();
            }
        }
        break;
    }
    case 'A': { // CUU (Cursor Up)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorUp;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'B': { // CUD (Cursor Down)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorDown;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'C': { // CUF (Cursor Forward)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorForward;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'D': { // CUB (Cursor Backward)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorBackward;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'E': { // CNL (Cursor Next Line)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorNextLine;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'F': { // CPL (Cursor Previous Line)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorPrevLine;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'G': { // CHA (Cursor Horizontal Absolute)
        int col = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::CursorHorizontalAbsolute;
        token.val1 = col;
        tokens.append(token);
        break;
    }
    case 'J': { // ED (Erase in Display)
        int mode = (params.isEmpty() || params.first().isEmpty()) ? 0 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::EraseInDisplay;
        token.val1 = mode;
        tokens.append(token);
        break;
    }
    case 'K': { // EL (Erase in Line)
        int mode = (params.isEmpty() || params.first().isEmpty()) ? 0 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::EraseInLine;
        token.val1 = mode;
        tokens.append(token);
        break;
    }
    case 's': { // SCP (Save Cursor Position)
        AnsiToken token;
        token.type = AnsiToken::SaveCursor;
        tokens.append(token);
        break;
    }
    case 'u': { // RCP (Restore Cursor Position)
        AnsiToken token;
        token.type = AnsiToken::RestoreCursor;
        tokens.append(token);
        break;
    }
    case 'P': { // DCH (Delete Character)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::DeleteCharacter;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'X': { // ECH (Erase Character)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::EraseCharacter;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'L': { // IL (Insert Line)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::InsertLine;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'M': { // DL (Delete Line)
        int count = (params.isEmpty() || params.first().isEmpty()) ? 1 : params.first().toInt();
        AnsiToken token;
        token.type = AnsiToken::DeleteLine;
        token.val1 = count;
        tokens.append(token);
        break;
    }
    case 'l': { // RM (Reset Mode) - e.g. ?25l Hide Cursor
        if (!params.isEmpty() && params.first() == "?25") {
            AnsiToken token;
            token.type = AnsiToken::HideCursor;
            tokens.append(token);
        }
        break;
    }
    case 'h': { // SM (Set Mode) - e.g. ?25h Show Cursor
        if (!params.isEmpty() && params.first() == "?25") {
            AnsiToken token;
            token.type = AnsiToken::ShowCursor;
            tokens.append(token);
        }
        break;
    }
    case 'H':
    case 'f': { // CUP (Cursor Position)
        int row = 1;
        int col = 1;
        if (params.size() >= 2) {
            row = params.at(0).isEmpty() ? 1 : params.at(0).toInt();
            col = params.at(1).isEmpty() ? 1 : params.at(1).toInt();
        } else if (params.size() == 1 && !params.at(0).isEmpty()) {
            row = params.at(0).toInt();
        }
        
        AnsiToken token;
        token.type = AnsiToken::CursorPosition;
        token.cursorRow = row;
        token.cursorCol = col;
        tokens.append(token);
        break;
    }
    default:
        qDebug() << "[AnsiParser] Ignored CSI sequence: cmd =" << cmd << "params =" << seqStr;
        break;
    }
}

QColor AnsiParser::getAnsiColor(int code, bool bright)
{
    // Standard ANSI 16-color palette mapping
    static const QColor normalColors[] = {
        QColor(0, 0, 0),       // Black
        QColor(205, 0, 0),     // Red
        QColor(0, 205, 0),     // Green
        QColor(205, 205, 0),   // Yellow
        QColor(0, 0, 238),     // Blue
        QColor(205, 0, 205),   // Magenta
        QColor(0, 205, 205),   // Cyan
        QColor(229, 229, 229)  // White (light gray)
    };

    static const QColor brightColors[] = {
        QColor(127, 127, 127), // Bright Black (Gray)
        QColor(255, 0, 0),     // Bright Red
        QColor(0, 255, 0),     // Bright Green
        QColor(255, 255, 0),   // Bright Yellow
        QColor(92, 92, 255),   // Bright Blue
        QColor(255, 0, 255),   // Bright Magenta
        QColor(0, 255, 255),   // Bright Cyan
        QColor(255, 255, 255)  // Bright White
    };

    if (code >= 0 && code < 8) {
        return bright ? brightColors[code] : normalColors[code];
    }
    return bright ? brightColors[7] : normalColors[7];
}

QColor AnsiParser::get256Color(int index)
{
    if (index >= 0 && index < 8) {
        return getAnsiColor(index, false);
    }
    if (index >= 8 && index < 16) {
        return getAnsiColor(index - 8, true);
    }
    if (index >= 16 && index <= 231) {
        int r = (index - 16) / 36;
        int g = ((index - 16) % 36) / 6;
        int b = (index - 16) % 6;
        static const int val[] = { 0, 95, 135, 175, 215, 255 };
        return QColor(val[r], val[g], val[b]);
    }
    if (index >= 232 && index <= 255) {
        int val = 8 + (index - 232) * 10;
        return QColor(val, val, val);
    }
    return QColor(Qt::white);
}
