#include "TerminalEditor.h"

#include "OTP.h"

#include <curses.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include <QStringList>

// NOTE: this file assumes a 16-bit wchar_t (true on Windows, where
// PDCursesMod's wincon backend runs) so QChar<->wchar_t conversion can be a
// straight reinterpret, no UTF-8/UTF-32 round trip needed. A Linux/ncurses
// build (wchar_t is 32-bit there) would need real conversion -- flagged for
// whenever this gets ported beyond the Windows console target.

namespace {

std::wstring toWStr(const QString &s)
{
    return std::wstring(reinterpret_cast<const wchar_t *>(s.utf16()), s.length());
}

QString fromWStr(const std::wstring &s)
{
    return QString::fromUtf16(reinterpret_cast<const ushort *>(s.c_str()), static_cast<int>(s.length()));
}

struct EditorState
{
    std::vector<std::wstring> lines{L""};
    int cursorRow = 0;
    int cursorCol = 0;
    int topLine = 0;  // vertical scroll offset
    int leftCol = 0;  // horizontal scroll offset
    bool readOnly = false;
};

void clampCursor(EditorState &st)
{
    if (st.cursorRow < 0)
        st.cursorRow = 0;
    if (st.cursorRow >= static_cast<int>(st.lines.size()))
        st.cursorRow = static_cast<int>(st.lines.size()) - 1;
    const int lineLen = static_cast<int>(st.lines[st.cursorRow].size());
    if (st.cursorCol < 0)
        st.cursorCol = 0;
    if (st.cursorCol > lineLen)
        st.cursorCol = lineLen;
}

void scrollToCursor(EditorState &st, int viewRows, int viewCols)
{
    if (st.cursorRow < st.topLine)
        st.topLine = st.cursorRow;
    if (st.cursorRow >= st.topLine + viewRows)
        st.topLine = st.cursorRow - viewRows + 1;
    if (st.cursorCol < st.leftCol)
        st.leftCol = st.cursorCol;
    if (st.cursorCol >= st.leftCol + viewCols)
        st.leftCol = st.cursorCol - viewCols + 1;
}

void draw(WINDOW *win, EditorState &st, const std::wstring &title)
{
    int maxY = 0, maxX = 0;
    getmaxyx(win, maxY, maxX);
    const int viewRows = maxY - 2; // top title bar + bottom status bar
    const int viewCols = maxX;

    scrollToCursor(st, viewRows, viewCols);
    werase(win);

    wattron(win, A_REVERSE);
    std::wstring titleLine = title;
    titleLine.resize(maxX, L' ');
    mvwaddnwstr(win, 0, 0, titleLine.c_str(), maxX);
    wattroff(win, A_REVERSE);

    for (int row = 0; row < viewRows; ++row) {
        const int lineIdx = st.topLine + row;
        if (lineIdx >= static_cast<int>(st.lines.size()))
            break;
        const std::wstring &line = st.lines[lineIdx];
        if (static_cast<int>(line.size()) > st.leftCol) {
            const std::wstring visible = line.substr(st.leftCol, viewCols);
            mvwaddnwstr(win, row + 1, 0, visible.c_str(), static_cast<int>(visible.size()));
        }
    }

    wattron(win, A_REVERSE);
    std::wstring status = st.readOnly
        ? std::wstring(L"[read only]  press any key to close")
        : std::wstring(L"F2 or Ctrl+S: save & exit    ESC: cancel    Ctrl+Z: undo    Ctrl+Y: redo    F1: help");
    status.resize(maxX, L' ');
    mvwaddnwstr(win, maxY - 1, 0, status.c_str(), maxX);
    wattroff(win, A_REVERSE);

    wmove(win, st.cursorRow - st.topLine + 1, st.cursorCol - st.leftCol);
    wrefresh(win);
}

// Draws a bordered, centred overlay box over whatever is currently on
// screen. Caller is responsible for reading whatever key(s) dismiss it.
void drawBox(WINDOW *win, const std::vector<std::wstring> &lines)
{
    int maxY = 0, maxX = 0;
    getmaxyx(win, maxY, maxX);
    size_t maxLen = 0;
    for (const std::wstring &l : lines)
        maxLen = std::max(maxLen, l.size());

    const int boxW = std::min(static_cast<int>(maxLen) + 4, maxX);
    const int boxH = std::min(static_cast<int>(lines.size()) + 2, maxY);
    const int bx = std::max(0, (maxX - boxW) / 2);
    const int by = std::max(0, (maxY - boxH) / 2);

    wattron(win, A_REVERSE);
    for (int row = 0; row < boxH; ++row) {
        std::wstring lineOut;
        if (row == 0 || row == boxH - 1) {
            lineOut = L"+" + std::wstring(std::max(0, boxW - 2), L'-') + L"+";
        } else {
            std::wstring content = static_cast<size_t>(row - 1) < lines.size() ? lines[row - 1] : L"";
            content.resize(std::max(0, boxW - 4), L' ');
            lineOut = L"| " + content + L" |";
        }
        mvwaddnwstr(win, by + row, bx, lineOut.c_str(), boxW);
    }
    wattroff(win, A_REVERSE);
    wrefresh(win);
}

// F1: a centred key-binding cheat sheet -- straight out of the original
// Python curses editor's HelpScreen, the classic 80s-TUI "press any key"
// popup.
void showHelp(WINDOW *win)
{
    static const std::vector<std::wstring> lines = {
        L"Keys",
        L"",
        L"F2 / Ctrl+S       Save and exit",
        L"Esc               Cancel",
        L"Arrow keys        Move cursor",
        L"Ctrl+Left/Right   Word left / right",
        L"Home / End        Line start / end",
        L"Ctrl+Home/End     Document start / end",
        L"PgUp / PgDn       Page up / down",
        L"Backspace/Delete  Delete character",
        L"Enter             New line",
        L"Ctrl+Z / Ctrl+Y   Undo / redo",
        L"F1                This help",
        L"",
        L"press any key to close",
    };
    drawBox(win, lines);
    wint_t ch = 0;
    wget_wch(win, &ch);
}

// Esc with unsaved changes: ask before throwing the edit away. Returns
// true if the caller should discard and close, false to keep editing.
bool confirmDiscard(WINDOW *win)
{
    static const std::vector<std::wstring> lines = {
        L"Discard unsaved changes?",
        L"",
        L"Y = discard      N / Esc = keep editing",
    };
    drawBox(win, lines);
    while (true) {
        wint_t ch = 0;
        const int keyType = wget_wch(win, &ch);
        if (keyType == OK) {
            if (ch == L'y' || ch == L'Y')
                return true;
            if (ch == L'n' || ch == L'N' || ch == 27)
                return false;
        }
    }
}

enum class EditorResult { Saved, Cancelled };

// Undo/redo history. Snapshots are taken before a mutation, keyed by a
// coarse "group" so that a run of ordinary typing (or a run of backspaces)
// collapses into a single undo step instead of one per keystroke; anything
// else (Enter, Delete, undo/redo itself) always starts a fresh group.
enum class EditGroup { None, Insert, Erase };

struct UndoHistory
{
    std::vector<EditorState> undoStack;
    std::vector<EditorState> redoStack;
    EditGroup lastGroup = EditGroup::None;

    void snapshotBefore(const EditorState &st, EditGroup group)
    {
        if (group == EditGroup::None || lastGroup != group) {
            undoStack.push_back(st);
            redoStack.clear();
        }
        lastGroup = group;
    }

    void undo(EditorState &st)
    {
        if (undoStack.empty())
            return;
        redoStack.push_back(st);
        st = undoStack.back();
        undoStack.pop_back();
        lastGroup = EditGroup::None;
    }

    void redo(EditorState &st)
    {
        if (redoStack.empty())
            return;
        undoStack.push_back(st);
        st = redoStack.back();
        redoStack.pop_back();
        lastGroup = EditGroup::None;
    }
};

void moveWordLeft(EditorState &st)
{
    // Ctrl+Left: skip any whitespace immediately to the left, then skip
    // the word itself, landing on the word's first character. At column 0
    // it just joins up with the end of the previous line.
    if (st.cursorCol == 0) {
        if (st.cursorRow > 0) {
            --st.cursorRow;
            st.cursorCol = static_cast<int>(st.lines[st.cursorRow].size());
        }
        return;
    }
    const std::wstring &line = st.lines[st.cursorRow];
    int col = st.cursorCol;
    while (col > 0 && std::iswspace(line[col - 1]))
        --col;
    while (col > 0 && !std::iswspace(line[col - 1]))
        --col;
    st.cursorCol = col;
}

void moveWordRight(EditorState &st)
{
    // Ctrl+Right: skip the rest of the current word, then any whitespace
    // after it, landing on the next word's first character. At end of
    // line it just moves to the start of the next line.
    const std::wstring &line = st.lines[st.cursorRow];
    const int len = static_cast<int>(line.size());
    if (st.cursorCol >= len) {
        if (st.cursorRow + 1 < static_cast<int>(st.lines.size())) {
            ++st.cursorRow;
            st.cursorCol = 0;
        }
        return;
    }
    int col = st.cursorCol;
    while (col < len && !std::iswspace(line[col]))
        ++col;
    while (col < len && std::iswspace(line[col]))
        ++col;
    st.cursorCol = col;
}

void eraseAt(EditorState &st, int row, int col)
{
    // shared backspace-at-(row,col) logic, used both for KEY_BACKSPACE and
    // the plain-character 0x08/0x7f some terminals send instead
    if (col > 0) {
        st.lines[row].erase(col - 1, 1);
        --st.cursorCol;
    } else if (row > 0) {
        const int prevLen = static_cast<int>(st.lines[row - 1].size());
        st.lines[row - 1] += st.lines[row];
        st.lines.erase(st.lines.begin() + row);
        --st.cursorRow;
        st.cursorCol = prevLen;
    }
}

EditorResult runEditorLoop(EditorState &st, const std::wstring &title)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    EditorResult result = EditorResult::Cancelled;
    bool finished = false;
    bool modified = false; // true once the user has typed/deleted anything
    UndoHistory history;

    while (!finished) {
        draw(stdscr, st, title);

        wint_t ch = 0;
        const int keyType = wget_wch(stdscr, &ch);

        if (st.readOnly) {
            result = EditorResult::Saved; // meaningless for read-only -- just "closed"
            break;
        }

        if (keyType == KEY_CODE_YES) {
            switch (ch) {
            case KEY_UP:    --st.cursorRow; break;
            case KEY_DOWN:  ++st.cursorRow; break;
            case KEY_LEFT:
                if (st.cursorCol > 0) {
                    --st.cursorCol;
                } else if (st.cursorRow > 0) {
                    --st.cursorRow;
                    st.cursorCol = static_cast<int>(st.lines[st.cursorRow].size());
                }
                break;
            case KEY_RIGHT:
                if (st.cursorCol < static_cast<int>(st.lines[st.cursorRow].size())) {
                    ++st.cursorCol;
                } else if (st.cursorRow + 1 < static_cast<int>(st.lines.size())) {
                    ++st.cursorRow;
                    st.cursorCol = 0;
                }
                break;
            case CTL_LEFT:  moveWordLeft(st); break;
            case CTL_RIGHT: moveWordRight(st); break;
            case KEY_HOME:  st.cursorCol = 0; break;
            case KEY_END:   st.cursorCol = static_cast<int>(st.lines[st.cursorRow].size()); break;
            case CTL_HOME:
                st.cursorRow = 0;
                st.cursorCol = 0;
                break;
            case CTL_END:
                st.cursorRow = static_cast<int>(st.lines.size()) - 1;
                st.cursorCol = static_cast<int>(st.lines[st.cursorRow].size());
                break;
            case KEY_PPAGE: st.cursorRow -= 10; break;
            case KEY_NPAGE: st.cursorRow += 10; break;
            case KEY_BACKSPACE:
                history.snapshotBefore(st, EditGroup::Erase);
                eraseAt(st, st.cursorRow, st.cursorCol);
                modified = true;
                break;
            case KEY_DC:
                history.snapshotBefore(st, EditGroup::Erase);
                if (st.cursorCol < static_cast<int>(st.lines[st.cursorRow].size())) {
                    st.lines[st.cursorRow].erase(st.cursorCol, 1);
                } else if (st.cursorRow + 1 < static_cast<int>(st.lines.size())) {
                    st.lines[st.cursorRow] += st.lines[st.cursorRow + 1];
                    st.lines.erase(st.lines.begin() + st.cursorRow + 1);
                }
                modified = true;
                break;
            case KEY_F(1):
                showHelp(stdscr);
                break;
            case KEY_F(2):
                result = EditorResult::Saved;
                finished = true;
                break;
            default:
                break;
            }
        } else if (keyType == OK) {
            if (ch == 27) { // ESC
                if (!modified || confirmDiscard(stdscr)) {
                    result = EditorResult::Cancelled;
                    finished = true;
                }
            } else if (ch == L'\r' || ch == L'\n') {
                history.snapshotBefore(st, EditGroup::None);
                std::wstring rest = st.lines[st.cursorRow].substr(st.cursorCol);
                st.lines[st.cursorRow].resize(st.cursorCol);
                st.lines.insert(st.lines.begin() + st.cursorRow + 1, rest);
                ++st.cursorRow;
                st.cursorCol = 0;
                modified = true;
            } else if (ch == 19) { // Ctrl+S
                result = EditorResult::Saved;
                finished = true;
            } else if (ch == 26) { // Ctrl+Z: undo
                history.undo(st);
            } else if (ch == 25) { // Ctrl+Y: redo
                history.redo(st);
            } else if (ch == 8 || ch == 127) {
                history.snapshotBefore(st, EditGroup::Erase);
                eraseAt(st, st.cursorRow, st.cursorCol);
                modified = true;
            } else if (OTP::isAllowedInputChar(QChar(static_cast<char16_t>(ch)))) {
                // Only the straddling checkerboard's own alphabet (Roman +
                // Cyrillic letters, digits, its punctuation set) is
                // encodable at all -- reject anything else here rather
                // than let it reach encode() and fail the whole message
                // at encipher time.
                history.snapshotBefore(st, EditGroup::Insert);
                st.lines[st.cursorRow].insert(st.cursorCol, 1, static_cast<wchar_t>(ch));
                ++st.cursorCol;
                modified = true;
            } else {
                beep();
            }
        }
        clampCursor(st);
    }

    endwin();
    return result;
}

} // namespace

QString TerminalEditor::getText(const QString &title)
{
    EditorState st;
    st.readOnly = false;
    const EditorResult result = runEditorLoop(st, toWStr(title));
    if (result == EditorResult::Cancelled)
        return QString(); // null -- caller checks .isNull()

    QString text;
    for (size_t i = 0; i < st.lines.size(); ++i) {
        text += fromWStr(st.lines[i]);
        if (i + 1 < st.lines.size())
            text += QLatin1Char('\n');
    }
    return text;
}

void TerminalEditor::showText(const QString &title, const QString &initialText)
{
    EditorState st;
    st.readOnly = true;
    st.lines.clear();
    for (const QString &part : initialText.split(QLatin1Char('\n')))
        st.lines.push_back(toWStr(part));
    if (st.lines.empty())
        st.lines.push_back(L"");

    runEditorLoop(st, toWStr(title));
}
