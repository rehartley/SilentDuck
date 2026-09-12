// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#include "TerminalEditor.h"
#include "Utf8.h"

#include <curses.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

// Internally this editor works entirely in std::u32string/char32_t -- one
// element per Unicode codepoint, fixed width on every platform (see Utf8.h's
// comment on why, and the note this replaces in Quacque's original
// TerminalEditor.cpp, which assumed a 16-bit wchar_t and flagged that as a
// gap for exactly this kind of port).
//
// The one place that still has to touch wchar_t at all is the handful of
// curses *wide* output calls (mvwaddnwstr() et al.), whose signature is
// defined in terms of wchar_t, not char32_t -- that's curses' API, not
// something this file can change. toWCurses() below is the single, narrow
// conversion point: every character this app's checkerboard alphabet can
// ever produce (Roman, Cyrillic, digits, punctuation) is well inside the
// Basic Multilingual Plane, so truncating char32_t to wchar_t is lossless
// there even on Windows, where wchar_t is only 16 bits -- on Linux/macOS
// (wchar_t is 32 bits) the same cast is a no-op. If this editor ever needs
// to display a codepoint outside the BMP, this is the one place that would
// need to grow a real UTF-16 surrogate-pair encoder for the Windows case.

namespace {

std::wstring toWCurses(const std::u32string &s)
{
    std::wstring w;
    w.reserve(s.size());
    for (char32_t cp : s)
        w.push_back(static_cast<wchar_t>(cp));
    return w;
}

struct EditorState
{
    std::vector<std::u32string> lines{U""};
    int cursorRow = 0;
    int cursorCol = 0;
    int topLine = 0;  // vertical scroll offset
    int leftCol = 0;  // horizontal scroll offset
    bool readOnly = false;
    // Empty = unrestricted, matching otp.py's allowed_chars=None. Set from
    // the allowedChars argument to getText()/showText() -- see
    // TerminalEditor.h.
    std::u32string allowedChars;
};

// Case-insensitive membership test against st.allowedChars; empty means no
// restriction at all (every printable character is allowed).
bool isCharAllowed(const EditorState &st, char32_t ch)
{
    if (st.allowedChars.empty())
        return true;
    return st.allowedChars.find(utf8::toUpper(ch)) != std::u32string::npos;
}

void clampCursor(EditorState &st)
{
    if (st.cursorRow < 0)
        st.cursorRow = 0;
    if (st.cursorRow >= static_cast<int>(st.lines.size()))
        st.cursorRow = static_cast<int>(st.lines.size()) - 1;
    const int lineLen = static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size());
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

void draw(WINDOW *win, EditorState &st, const std::u32string &title)
{
    int maxY = 0, maxX = 0;
    getmaxyx(win, maxY, maxX);
    const int viewRows = maxY - 2; // top title bar + bottom status bar
    const int viewCols = maxX;

    scrollToCursor(st, viewRows, viewCols);
    werase(win);

    wattron(win, A_REVERSE);
    std::u32string titleLine = title;
    titleLine.resize(static_cast<std::size_t>(maxX), U' ');
    const std::wstring titleW = toWCurses(titleLine);
    mvwaddnwstr(win, 0, 0, titleW.c_str(), maxX);
    wattroff(win, A_REVERSE);

    for (int row = 0; row < viewRows; ++row) {
        const int lineIdx = st.topLine + row;
        if (lineIdx >= static_cast<int>(st.lines.size()))
            break;
        const std::u32string &line = st.lines[static_cast<std::size_t>(lineIdx)];
        if (static_cast<int>(line.size()) > st.leftCol) {
            const std::u32string visible = line.substr(static_cast<std::size_t>(st.leftCol), static_cast<std::size_t>(viewCols));
            const std::wstring visibleW = toWCurses(visible);
            mvwaddnwstr(win, row + 1, 0, visibleW.c_str(), static_cast<int>(visibleW.size()));
        }
    }

    wattron(win, A_REVERSE);
    std::u32string status = st.readOnly
        ? std::u32string(U"[read only]  press any key to close")
        : std::u32string(U"F2 or Ctrl+S: save & exit    ESC: cancel    Ctrl+Z: undo    Ctrl+Y: redo    F1: help    F5: paste alphabet");
    status.resize(static_cast<std::size_t>(maxX), U' ');
    const std::wstring statusW = toWCurses(status);
    mvwaddnwstr(win, maxY - 1, 0, statusW.c_str(), maxX);
    wattroff(win, A_REVERSE);

    wmove(win, st.cursorRow - st.topLine + 1, st.cursorCol - st.leftCol);
    wrefresh(win);
}

// Draws a bordered, centred overlay box over whatever is currently on
// screen. Caller is responsible for reading whatever key(s) dismiss it.
void drawBox(WINDOW *win, const std::vector<std::u32string> &lines)
{
    int maxY = 0, maxX = 0;
    getmaxyx(win, maxY, maxX);
    std::size_t maxLen = 0;
    for (const std::u32string &l : lines)
        maxLen = std::max(maxLen, l.size());

    const int boxW = std::min(static_cast<int>(maxLen) + 4, maxX);
    const int boxH = std::min(static_cast<int>(lines.size()) + 2, maxY);
    const int bx = std::max(0, (maxX - boxW) / 2);
    const int by = std::max(0, (maxY - boxH) / 2);

    wattron(win, A_REVERSE);
    for (int row = 0; row < boxH; ++row) {
        std::u32string lineOut;
        if (row == 0 || row == boxH - 1) {
            lineOut = U"+" + std::u32string(static_cast<std::size_t>(std::max(0, boxW - 2)), U'-') + U"+";
        } else {
            std::u32string content = static_cast<std::size_t>(row - 1) < lines.size() ? lines[static_cast<std::size_t>(row - 1)] : U"";
            content.resize(static_cast<std::size_t>(std::max(0, boxW - 4)), U' ');
            lineOut = U"| " + content + U" |";
        }
        const std::wstring lineOutW = toWCurses(lineOut);
        mvwaddnwstr(win, by + row, bx, lineOutW.c_str(), boxW);
    }
    wattroff(win, A_REVERSE);
    wrefresh(win);
}

// F1: a centred key-binding cheat sheet -- the classic 80s-TUI "press any
// key" popup.
void showHelp(WINDOW *win)
{
    static const std::vector<std::u32string> lines = {
        U"Keys",
        U"",
        U"F2 / Ctrl+S       Save and exit",
        U"Esc               Cancel",
        U"Arrow keys        Move cursor",
        U"Ctrl+Left/Right   Word left / right",
        U"Home / End        Line start / end",
        U"Ctrl+Home/End     Document start / end",
        U"PgUp / PgDn       Page up / down",
        U"Backspace/Delete  Delete character",
        U"Enter             New line",
        U"Ctrl+Z / Ctrl+Y   Undo / redo",
        U"F1                This help",
        U"F5                Paste allowed alphabet",
        U"",
        U"press any key to close",
    };
    drawBox(win, lines);
    wint_t ch = 0;
    wget_wch(win, &ch);
}

// Esc with unsaved changes: ask before throwing the edit away. Returns true
// if the caller should discard and close, false to keep editing.
bool confirmDiscard(WINDOW *win)
{
    static const std::vector<std::u32string> lines = {
        U"Discard unsaved changes?",
        U"",
        U"Y = discard      N / Esc = keep editing",
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

// Only called from the CTL_LEFT/CTL_RIGHT cases below, which are themselves
// compiled out on curses implementations (ncursesw) that don't define those
// PDCursesMod-specific key codes -- guarded the same way here so these two
// don't turn into unused-function warnings on that platform.
#ifdef CTL_LEFT
void moveWordLeft(EditorState &st)
{
    // Ctrl+Left: skip any whitespace immediately to the left, then skip the
    // word itself, landing on the word's first character. At column 0 it
    // just joins up with the end of the previous line.
    if (st.cursorCol == 0) {
        if (st.cursorRow > 0) {
            --st.cursorRow;
            st.cursorCol = static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size());
        }
        return;
    }
    const std::u32string &line = st.lines[static_cast<std::size_t>(st.cursorRow)];
    int col = st.cursorCol;
    while (col > 0 && std::iswspace(static_cast<wint_t>(line[static_cast<std::size_t>(col - 1)])))
        --col;
    while (col > 0 && !std::iswspace(static_cast<wint_t>(line[static_cast<std::size_t>(col - 1)])))
        --col;
    st.cursorCol = col;
}
#endif // CTL_LEFT

#ifdef CTL_RIGHT
void moveWordRight(EditorState &st)
{
    // Ctrl+Right: skip the rest of the current word, then any whitespace
    // after it, landing on the next word's first character. At end of line
    // it just moves to the start of the next line.
    const std::u32string &line = st.lines[static_cast<std::size_t>(st.cursorRow)];
    const int len = static_cast<int>(line.size());
    if (st.cursorCol >= len) {
        if (st.cursorRow + 1 < static_cast<int>(st.lines.size())) {
            ++st.cursorRow;
            st.cursorCol = 0;
        }
        return;
    }
    int col = st.cursorCol;
    while (col < len && !std::iswspace(static_cast<wint_t>(line[static_cast<std::size_t>(col)])))
        ++col;
    while (col < len && std::iswspace(static_cast<wint_t>(line[static_cast<std::size_t>(col)])))
        ++col;
    st.cursorCol = col;
}
#endif // CTL_RIGHT

// Inserts text at the cursor, splitting on '\n' into new lines. Used by F5
// to drop the checkerboard's whole alphabet into the buffer -- see the
// KEY_F(5) case below -- so a user who can't recall how to type one of its
// characters can copy it out instead.
void pasteText(EditorState &st, const std::u32string &text)
{
    if (text.empty())
        return;
    std::vector<std::u32string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == U'\n') {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    std::u32string &curLine = st.lines[static_cast<std::size_t>(st.cursorRow)];
    if (parts.size() == 1) {
        curLine.insert(static_cast<std::size_t>(st.cursorCol), parts[0]);
        st.cursorCol += static_cast<int>(parts[0].size());
    } else {
        std::u32string tail = curLine.substr(static_cast<std::size_t>(st.cursorCol));
        curLine.resize(static_cast<std::size_t>(st.cursorCol));
        curLine += parts[0];
        for (std::size_t i = 1; i < parts.size(); ++i)
            st.lines.insert(st.lines.begin() + st.cursorRow + static_cast<int>(i), parts[i]);
        const int lastRow = st.cursorRow + static_cast<int>(parts.size()) - 1;
        st.lines[static_cast<std::size_t>(lastRow)] += tail;
        st.cursorRow = lastRow;
        st.cursorCol = static_cast<int>(parts.back().size());
    }
}

void eraseAt(EditorState &st, int row, int col)
{
    // shared backspace-at-(row,col) logic, used both for KEY_BACKSPACE and
    // the plain-character 0x08/0x7f some terminals send instead
    if (col > 0) {
        st.lines[static_cast<std::size_t>(row)].erase(static_cast<std::size_t>(col - 1), 1);
        --st.cursorCol;
    } else if (row > 0) {
        const int prevLen = static_cast<int>(st.lines[static_cast<std::size_t>(row - 1)].size());
        st.lines[static_cast<std::size_t>(row - 1)] += st.lines[static_cast<std::size_t>(row)];
        st.lines.erase(st.lines.begin() + row);
        --st.cursorRow;
        st.cursorCol = prevLen;
    }
}

EditorResult runEditorLoop(EditorState &st, const std::u32string &title)
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
                    st.cursorCol = static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size());
                }
                break;
            case KEY_RIGHT:
                if (st.cursorCol < static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size())) {
                    ++st.cursorCol;
                } else if (st.cursorRow + 1 < static_cast<int>(st.lines.size())) {
                    ++st.cursorRow;
                    st.cursorCol = 0;
                }
                break;
            // CTL_LEFT/CTL_RIGHT/CTL_HOME/CTL_END (Ctrl+arrow word-jump,
            // Ctrl+Home/End document-jump) are PDCursesMod extensions, not
            // part of standard curses -- ncursesw doesn't define them, so
            // each is guarded individually rather than assumed present.
            // Word/document jump this way just isn't available under
            // ncursesw; everything else in this editor is unaffected.
#ifdef CTL_LEFT
            case CTL_LEFT:  moveWordLeft(st); break;
#endif
#ifdef CTL_RIGHT
            case CTL_RIGHT: moveWordRight(st); break;
#endif
            case KEY_HOME:  st.cursorCol = 0; break;
            case KEY_END:   st.cursorCol = static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size()); break;
#ifdef CTL_HOME
            case CTL_HOME:
                st.cursorRow = 0;
                st.cursorCol = 0;
                break;
#endif
#ifdef CTL_END
            case CTL_END:
                st.cursorRow = static_cast<int>(st.lines.size()) - 1;
                st.cursorCol = static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size());
                break;
#endif
            case KEY_PPAGE: st.cursorRow -= 10; break;
            case KEY_NPAGE: st.cursorRow += 10; break;
            case KEY_BACKSPACE:
                history.snapshotBefore(st, EditGroup::Erase);
                eraseAt(st, st.cursorRow, st.cursorCol);
                modified = true;
                break;
            case KEY_DC:
                history.snapshotBefore(st, EditGroup::Erase);
                if (st.cursorCol < static_cast<int>(st.lines[static_cast<std::size_t>(st.cursorRow)].size())) {
                    st.lines[static_cast<std::size_t>(st.cursorRow)].erase(static_cast<std::size_t>(st.cursorCol), 1);
                } else if (st.cursorRow + 1 < static_cast<int>(st.lines.size())) {
                    st.lines[static_cast<std::size_t>(st.cursorRow)] += st.lines[static_cast<std::size_t>(st.cursorRow + 1)];
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
            case KEY_F(5): {
                // Paste allowedChars itself, for anyone who can't recall how
                // to type one of its more exotic characters.
                if (!st.allowedChars.empty()) {
                    history.snapshotBefore(st, EditGroup::None);
                    pasteText(st, st.allowedChars);
                    modified = true;
                }
                break;
            }
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
                std::u32string rest = st.lines[static_cast<std::size_t>(st.cursorRow)].substr(static_cast<std::size_t>(st.cursorCol));
                st.lines[static_cast<std::size_t>(st.cursorRow)].resize(static_cast<std::size_t>(st.cursorCol));
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
            } else if (isCharAllowed(st, static_cast<char32_t>(ch))) {
                // allowedChars gates what's typeable at all -- when set to
                // OTP::allowedInputChars() (see OtpCli.cpp), this rejects
                // anything outside the straddling checkerboard's own
                // alphabet here rather than letting it reach encode() and
                // fail the whole message at encipher time.
                history.snapshotBefore(st, EditGroup::Insert);
                st.lines[static_cast<std::size_t>(st.cursorRow)].insert(static_cast<std::size_t>(st.cursorCol), 1, static_cast<char32_t>(ch));
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

std::optional<std::u32string> TerminalEditor::getText(const std::u32string &title, const std::u32string &allowedChars)
{
    EditorState st;
    st.readOnly = false;
    st.allowedChars = allowedChars;
    const EditorResult result = runEditorLoop(st, title);
    if (result == EditorResult::Cancelled)
        return std::nullopt;

    std::u32string text;
    for (std::size_t i = 0; i < st.lines.size(); ++i) {
        text += st.lines[i];
        if (i + 1 < st.lines.size())
            text += U'\n';
    }
    return text;
}

void TerminalEditor::showText(const std::u32string &title, const std::u32string &initialText, const std::u32string &allowedChars)
{
    EditorState st;
    st.readOnly = true;
    st.allowedChars = allowedChars; // no effect in read-only mode; see TerminalEditor.h
    st.lines.clear();

    std::size_t start = 0;
    for (std::size_t i = 0; i <= initialText.size(); ++i) {
        if (i == initialText.size() || initialText[i] == U'\n') {
            st.lines.push_back(initialText.substr(start, i - start));
            start = i + 1;
        }
    }
    if (st.lines.empty())
        st.lines.push_back(U"");

    runEditorLoop(st, title);
}
