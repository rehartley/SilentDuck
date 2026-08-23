#!/usr/bin/python3

"""pytextedit.py  —  Reusable curses multi-line text editor widget.

Import and use inside your own curses application:

    from pytextedit import ConsoleTextEditor, editString

    # Quick one-liner
    result = editString("initial text", filename="notes.txt")

    # Embedded window (x, y, w, h in terminal cell coordinates)
    ed = ConsoleTextEditor("hello", filename="test.txt", x=5, y=2, w=60, h=20)
    result = ed.editString()

Run as a standalone text-file editor:

    python pytextedit.py [filename]

Requirements: Python 3.9+, curses (stdlib).
On Windows:   pip install windows-curses
"""

from __future__ import annotations

import curses # pip install windows-curses
import os
import re
import signal
import sys
from typing import List, Optional, Tuple


# Module-level clipboard — persists across editor sessions; callable code can
# pre-populate it via ConsoleTextEditor.clipboard before calling editString().
_clipboard: List[str] = ['']   # mutable so inner functions can write without 'global'


# ---------------------------------------------------------------------------
# Color pair indices  (initialised once by _init_colors)
# ---------------------------------------------------------------------------
_CP_EDITOR    = 1   # fg on bg   — normal editor text
_CP_STATUS    = 2   # bg on fg   — status bar (true inverse of editor)
_CP_SELECT    = 3   # bg on fg   — selected text (true inverse)
_CP_DIALOG    = 4   # fg on bg   — find / replace bar
_CP_DIALOG_HI = 5   # fg on bg   — dialog labels / borders (+ A_BOLD)
_CP_WRAP      = 6   # fg on bg   — soft-wrap indicator ↵   (+ A_DIM)


# ---------------------------------------------------------------------------
# Color schemes  —  (fg_color, bg_color, use_bold)
# Default background is black; -blue variants use the classic DOS/TP blue.
# ---------------------------------------------------------------------------
_COLOR_SCHEMES: dict = {
    # Black background (default)
    'green':       (curses.COLOR_GREEN,  curses.COLOR_BLACK, True),
    'amber':       (curses.COLOR_YELLOW, curses.COLOR_BLACK, False),
    'white':       (curses.COLOR_WHITE,  curses.COLOR_BLACK, True),
    'yellow':      (curses.COLOR_YELLOW, curses.COLOR_BLACK, True),
    # Blue background (classic Turbo Pascal / Norton Commander aesthetic)
    'green-blue':  (curses.COLOR_GREEN,  curses.COLOR_BLUE,  True),
    'amber-blue':  (curses.COLOR_YELLOW, curses.COLOR_BLUE,  False),
    'white-blue':  (curses.COLOR_WHITE,  curses.COLOR_BLUE,  True),
    'yellow-blue': (curses.COLOR_YELLOW, curses.COLOR_BLUE,  True),
}


def _init_colors(fg_color: int, bg_color: int) -> None:
    """Initialise curses color pairs for the chosen fg / bg colors.
    Falls back to monochrome silently if the terminal has no color support
    (e.g. TERM=vt102 — color_pair(N) returns 0, editor still functional).
    """
    if not curses.has_colors():
        return
    curses.start_color()
    try:
        curses.use_default_colors()  # not supported on all terminals; safe to skip
    except curses.error:
        pass
    curses.init_pair(_CP_EDITOR,    fg_color,  bg_color)
    curses.init_pair(_CP_STATUS,    bg_color,  fg_color)
    curses.init_pair(_CP_SELECT,    bg_color,  fg_color)
    curses.init_pair(_CP_DIALOG,    fg_color,  bg_color)
    curses.init_pair(_CP_DIALOG_HI, fg_color,  bg_color)
    curses.init_pair(_CP_WRAP,      fg_color,  bg_color)


# ---------------------------------------------------------------------------
# Type aliases
# ---------------------------------------------------------------------------
_Pos      = Tuple[int, int]                         # (row, col) in logical text
_Snapshot = Tuple[List[str], _Pos, Optional[_Pos]]  # lines, cursor, sel-anchor


# ---------------------------------------------------------------------------
# TextBuffer  —  all text/editing state; zero curses dependency
# ---------------------------------------------------------------------------
class TextBuffer:
    """
    Stores document text as a list of strings (logical lines, no '\\n').

    cursor      — (row, col) insertion point
    _anchor     — selection anchor; None = no active selection.
                  Selection spans from _anchor to cursor (order varies).
    insert_mode — True = insert characters, False = overwrite
    """

    _OP_NONE = ''
    _OP_TYPE = 'type'   # consecutive character inserts → one undo unit
    _OP_BKSP = 'bksp'  # consecutive backspaces → one undo unit

    def __init__(self, text: str = '') -> None:
        self.text: List[str]           = text.split('\n') if text else ['']
        self.cursor: _Pos              = (0, 0)
        self._anchor: Optional[_Pos]  = None
        self.undo_stack: List[_Snapshot] = []
        self.redo_stack: List[_Snapshot] = []
        self.insert_mode: bool         = True
        self._last_op: str             = self._OP_NONE

    # ------------------------------------------------------------------
    # Serialisation
    # ------------------------------------------------------------------

    def get_text(self) -> str:
        """Return the full document as a single '\\n'-delimited string."""
        return '\n'.join(self.text)

    # ------------------------------------------------------------------
    # Undo / Redo
    # ------------------------------------------------------------------

    def _snapshot(self) -> _Snapshot:
        return (list(self.text), self.cursor, self._anchor)

    def _push_undo(self) -> None:
        self.undo_stack.append(self._snapshot())
        self.redo_stack.clear()

    def commit(self) -> None:
        """Force an undo checkpoint.  Call before bulk/structural operations."""
        self._push_undo()
        self._last_op = self._OP_NONE

    def _checkpoint(self, op: str) -> None:
        """Save a snapshot only when the operation type changes."""
        if op != self._last_op:
            self._push_undo()
            self._last_op = op

    def undo(self) -> bool:
        if not self.undo_stack:
            return False
        self.redo_stack.append(self._snapshot())
        snap = self.undo_stack.pop()
        self.text, self.cursor, self._anchor = snap[0], snap[1], snap[2]
        self._last_op = self._OP_NONE
        return True

    def redo(self) -> bool:
        if not self.redo_stack:
            return False
        self.undo_stack.append(self._snapshot())
        snap = self.redo_stack.pop()
        self.text, self.cursor, self._anchor = snap[0], snap[1], snap[2]
        self._last_op = self._OP_NONE
        return True

    # ------------------------------------------------------------------
    # Selection helpers
    # ------------------------------------------------------------------

    def _has_sel(self) -> bool:
        return self._anchor is not None

    def sel_range(self) -> Optional[Tuple[_Pos, _Pos]]:
        """Return (start, end) normalised so start <= end, or None."""
        if self._anchor is None:
            return None
        a, b = self._anchor, self.cursor
        return (a, b) if a <= b else (b, a)

    def clear_sel(self) -> None:
        self._anchor = None

    # ------------------------------------------------------------------
    # Cursor helpers
    # ------------------------------------------------------------------

    def _clamp(self, row: int, col: int) -> _Pos:
        row = max(0, min(row, len(self.text) - 1))
        col = max(0, min(col, len(self.text[row])))
        return (row, col)

    def set_cursor(self, row: int, col: int, extend_sel: bool = False) -> None:
        """Move cursor to (row, col), optionally extending the selection."""
        if extend_sel:
            if self._anchor is None:
                self._anchor = self.cursor
        else:
            self._anchor = None
        self.cursor = self._clamp(row, col)

    # ------------------------------------------------------------------
    # Cursor movement
    # ------------------------------------------------------------------

    def move_left(self, extend_sel: bool = False) -> None:
        r, c = self.cursor
        if not extend_sel and self._has_sel():
            sr = self.sel_range()
            assert sr is not None
            self._anchor = None
            self.cursor = sr[0]
            return
        if extend_sel and self._anchor is None:
            self._anchor = self.cursor
        elif not extend_sel:
            self._anchor = None
        if c > 0:
            self.cursor = (r, c - 1)
        elif r > 0:
            self.cursor = (r - 1, len(self.text[r - 1]))

    def move_right(self, extend_sel: bool = False) -> None:
        r, c = self.cursor
        if not extend_sel and self._has_sel():
            sr = self.sel_range()
            assert sr is not None
            self._anchor = None
            self.cursor = sr[1]
            return
        if extend_sel and self._anchor is None:
            self._anchor = self.cursor
        elif not extend_sel:
            self._anchor = None
        if c < len(self.text[r]):
            self.cursor = (r, c + 1)
        elif r < len(self.text) - 1:
            self.cursor = (r + 1, 0)

    def move_home(self, extend_sel: bool = False) -> None:
        self.set_cursor(self.cursor[0], 0, extend_sel)

    def move_end(self, extend_sel: bool = False) -> None:
        r = self.cursor[0]
        self.set_cursor(r, len(self.text[r]), extend_sel)

    def move_doc_start(self, extend_sel: bool = False) -> None:
        self.set_cursor(0, 0, extend_sel)

    def move_doc_end(self, extend_sel: bool = False) -> None:
        last = len(self.text) - 1
        self.set_cursor(last, len(self.text[last]), extend_sel)

    @staticmethod
    def _is_word(ch: str) -> bool:
        return ch.isalnum() or ch == '_'

    def move_word_left(self, extend_sel: bool = False) -> None:
        if extend_sel and self._anchor is None:
            self._anchor = self.cursor
        elif not extend_sel:
            self._anchor = None
        r, c = self.cursor
        # Step back one position first
        if c > 0:
            c -= 1
        elif r > 0:
            r -= 1
            c = len(self.text[r])
        else:
            return  # already at document start
        # Skip non-word chars moving backwards
        while True:
            if c < len(self.text[r]) and self._is_word(self.text[r][c]):
                break
            if c > 0:
                c -= 1
            elif r > 0:
                r -= 1
                c = len(self.text[r])
            else:
                self.cursor = (0, 0)
                return
        # Skip word chars moving backwards to find word start
        while c > 0 and self._is_word(self.text[r][c - 1]):
            c -= 1
        self.cursor = (r, c)

    def move_word_right(self, extend_sel: bool = False) -> None:
        if extend_sel and self._anchor is None:
            self._anchor = self.cursor
        elif not extend_sel:
            self._anchor = None
        r, c = self.cursor
        line = self.text[r]
        # Skip current word chars
        while c < len(line) and self._is_word(line[c]):
            c += 1
        # Skip non-word chars
        while c < len(line) and not self._is_word(line[c]):
            c += 1
        # If at end of line, advance to start of next line
        if c >= len(line) and r < len(self.text) - 1:
            r += 1
            c = 0
        self.cursor = (r, c)

    # ------------------------------------------------------------------
    # Selection operations
    # ------------------------------------------------------------------

    def select_all(self) -> None:
        self._anchor = (0, 0)
        last = len(self.text) - 1
        self.cursor = (last, len(self.text[last]))

    def _get_sel_text(self) -> str:
        sr = self.sel_range()
        if sr is None:
            return ''
        (r1, c1), (r2, c2) = sr
        if r1 == r2:
            return self.text[r1][c1:c2]
        parts = [self.text[r1][c1:]]
        for r in range(r1 + 1, r2):
            parts.append(self.text[r])
        parts.append(self.text[r2][:c2])
        return '\n'.join(parts)

    def _delete_sel(self) -> None:
        """Delete selected text and collapse cursor to selection start."""
        sr = self.sel_range()
        if sr is None:
            return
        (r1, c1), (r2, c2) = sr
        if r1 == r2:
            self.text[r1] = self.text[r1][:c1] + self.text[r1][c2:]
        else:
            self.text[r1] = self.text[r1][:c1] + self.text[r2][c2:]
            del self.text[r1 + 1:r2 + 1]
        self.cursor = (r1, c1)
        self._anchor = None

    def copy(self) -> str:
        """Return selected text without modifying the buffer."""
        return self._get_sel_text()

    def cut(self) -> str:
        """Delete and return selected text."""
        text = self._get_sel_text()
        if text:
            self.commit()
            self._delete_sel()
        return text

    def paste(self, text: str) -> None:
        """Insert *text* at cursor, replacing any active selection."""
        if not text:
            return
        self.commit()
        if self._has_sel():
            self._delete_sel()
        lines = text.split('\n')
        r, c = self.cursor
        if len(lines) == 1:
            self.text[r] = self.text[r][:c] + lines[0] + self.text[r][c:]
            self.cursor = (r, c + len(lines[0]))
        else:
            tail = self.text[r][c:]
            self.text[r] = self.text[r][:c] + lines[0]
            for i, part in enumerate(lines[1:], 1):
                self.text.insert(r + i, part)
            last_r = r + len(lines) - 1
            self.text[last_r] += tail
            self.cursor = (last_r, len(lines[-1]))

    # ------------------------------------------------------------------
    # Editing
    # ------------------------------------------------------------------

    def insert_char(self, ch: str) -> None:
        if self._has_sel():
            self.commit()
            self._delete_sel()
            self._last_op = self._OP_NONE
        else:
            self._checkpoint(self._OP_TYPE)
        r, c = self.cursor
        if self.insert_mode:
            self.text[r] = self.text[r][:c] + ch + self.text[r][c:]
        else:
            line = self.text[r]
            self.text[r] = line[:c] + ch + (line[c + 1:] if c < len(line) else '')
        self.cursor = (r, min(c + 1, len(self.text[r])))

    def insert_newline(self) -> None:
        self.commit()
        if self._has_sel():
            self._delete_sel()
        r, c = self.cursor
        tail = self.text[r][c:]
        self.text[r] = self.text[r][:c]
        self.text.insert(r + 1, tail)
        self.cursor = (r + 1, 0)

    def backspace(self) -> None:
        if self._has_sel():
            self.commit()
            self._delete_sel()
            return
        r, c = self.cursor
        if c == 0 and r == 0:
            return
        self._checkpoint(self._OP_BKSP)
        if c > 0:
            self.text[r] = self.text[r][:c - 1] + self.text[r][c:]
            self.cursor = (r, c - 1)
        else:
            prev_len = len(self.text[r - 1])
            self.text[r - 1] += self.text[r]
            del self.text[r]
            self.cursor = (r - 1, prev_len)

    def delete_char(self) -> None:
        if self._has_sel():
            self.commit()
            self._delete_sel()
            return
        r, c = self.cursor
        if r == len(self.text) - 1 and c == len(self.text[r]):
            return
        self.commit()
        if c < len(self.text[r]):
            self.text[r] = self.text[r][:c] + self.text[r][c + 1:]
        else:
            self.text[r] += self.text[r + 1]
            del self.text[r + 1]

    def delete_word_left(self) -> None:
        if self._has_sel():
            self.commit()
            self._delete_sel()
            return
        old = self.cursor
        self.move_word_left()
        if self.cursor == old:
            return
        start = min(self.cursor, old)
        end   = max(self.cursor, old)
        self.commit()
        self._anchor = start
        self.cursor  = end
        self._delete_sel()

    def delete_word_right(self) -> None:
        if self._has_sel():
            self.commit()
            self._delete_sel()
            return
        old = self.cursor
        self.move_word_right()
        if self.cursor == old:
            return
        start = min(old, self.cursor)
        end   = max(old, self.cursor)
        self.commit()
        self._anchor = start
        self.cursor  = end
        self._delete_sel()

    # ------------------------------------------------------------------
    # Find / Replace
    # ------------------------------------------------------------------

    def find(self, needle: str, from_pos: _Pos, *,
             wrap: bool = True, case_sensitive: bool = False) -> Optional[_Pos]:
        """Search for *needle* starting just after *from_pos*.
        Returns (row, col) of the match start, or None."""
        if not needle:
            return None
        flags = 0 if case_sensitive else re.IGNORECASE
        pat   = re.compile(re.escape(needle), flags)
        rows  = len(self.text)
        r0, c0 = from_pos
        # Forward: from_pos → end of file
        for ri in range(r0, rows):
            line  = self.text[ri]
            start = c0 if ri == r0 else 0
            m = pat.search(line, start)
            if m:
                return (ri, m.start())
        # Wrap: beginning → from_pos (exclusive)
        if wrap:
            for ri in range(0, r0 + 1):
                line = self.text[ri]
                end  = c0 if ri == r0 else len(line)
                m = pat.search(line[:end])
                if m:
                    return (ri, m.start())
        return None

    def replace_all(self, needle: str, replacement: str, *,
                    case_sensitive: bool = False) -> int:
        """Replace every occurrence of *needle*; returns replacement count."""
        if not needle:
            return 0
        self.commit()
        flags   = 0 if case_sensitive else re.IGNORECASE
        pattern = re.compile(re.escape(needle), flags)
        count   = 0
        for i, line in enumerate(self.text):
            new_line, n = pattern.subn(replacement, line)
            self.text[i] = new_line
            count += n
        return count


# ---------------------------------------------------------------------------
# SoftWrapper  —  maps logical lines <-> visual (display) rows
# ---------------------------------------------------------------------------
class SoftWrapper:
    """
    Computes soft-wrap layout for a list of logical lines.

    wrap_width   — usable text columns (terminal width - 1 for the wrap marker)
    display_rows — list of (logical_row, col_offset) per visual row.
                   col_offset is the index into text[logical_row] where the
                   visual row begins.
    """

    WRAP_CHAR = '\u21b5'  # rendered at the right edge on every wrapped segment

    def __init__(self, lines: List[str], wrap_width: int) -> None:
        self.wrap_width = max(1, wrap_width)
        self.display_rows: List[_Pos] = []
        self._rebuild(lines)

    def rebuild(self, lines: List[str], wrap_width: Optional[int] = None) -> None:
        if wrap_width is not None:
            self.wrap_width = max(1, wrap_width)
        self._rebuild(lines)

    def _rebuild(self, lines: List[str]) -> None:
        self.display_rows = []
        w = self.wrap_width
        for log_row, line in enumerate(lines):
            if not line:
                self.display_rows.append((log_row, 0))
            else:
                offset = 0
                while offset < len(line):
                    self.display_rows.append((log_row, offset))
                    offset += w

    def to_display(self, log_row: int, log_col: int) -> _Pos:
        """Convert logical (row, col) -> visual (vis_row, vis_col)."""
        w = self.wrap_width
        best = 0
        for i, (lr, off) in enumerate(self.display_rows):
            if lr == log_row:
                if off <= log_col:
                    best = i
                else:
                    break
        _, off = self.display_rows[best]
        return (best, log_col - off)

    def to_logical(self, vis_row: int, vis_col: int) -> _Pos:
        """Convert visual (vis_row, vis_col) -> logical (log_row, log_col)."""
        if not self.display_rows:
            return (0, 0)
        vis_row = max(0, min(vis_row, len(self.display_rows) - 1))
        log_row, off = self.display_rows[vis_row]
        return (log_row, off + vis_col)

    def vis_row_count(self) -> int:
        return len(self.display_rows)

    def first_vis_row_of(self, log_row: int) -> int:
        for i, (lr, _) in enumerate(self.display_rows):
            if lr == log_row:
                return i
        return 0

    def last_vis_row_of(self, log_row: int) -> int:
        result = 0
        for i, (lr, _) in enumerate(self.display_rows):
            if lr == log_row:
                result = i
        return result

    def is_continuation(self, vis_row: int) -> bool:
        """True if this visual row is a soft-wrap continuation of the row above."""
        if vis_row <= 0 or vis_row >= len(self.display_rows):
            return False
        _, off = self.display_rows[vis_row]
        return off > 0


# ---------------------------------------------------------------------------
# StatusBar
# ---------------------------------------------------------------------------
class StatusBar:
    """Renders a one-line status bar at a fixed screen row."""

    def __init__(self, win: 'curses.window', screen_row: int,
                 width: int) -> None:
        self._win   = win
        self._row   = screen_row
        self._width = width

    def render(self, filename: str, buf: TextBuffer) -> None:
        r, c        = buf.cursor
        line_count  = len(buf.text)
        mode        = 'OVR' if not buf.insert_mode else 'INS'
        fname       = (filename or '[no name]')[:20]
        left        = f' {fname} '
        mid         = f' Ln {r + 1}, Col {c + 1} | {line_count} lines '
        right       = f' {mode} | F2=Exit | F1=Help '
        gap         = self._width - len(left) - len(mid) - len(right)
        if gap < 0:
            mid  = f' {r + 1}:{c + 1} '
            gap  = self._width - len(left) - len(mid) - len(right)
        bar  = left + mid + ' ' * max(0, gap) + right
        bar  = bar[:self._width].ljust(self._width)
        attr = curses.color_pair(_CP_STATUS)
        try:
            self._win.addstr(self._row, 0, bar, attr)
        except curses.error:
            pass


# ---------------------------------------------------------------------------
# FindBar
# ---------------------------------------------------------------------------
class FindBar:
    """
    Interactive find / replace bar at the bottom of the editor area.
    run_find() and run_replace() drive their own mini event-loops.
    """

    def __init__(self, win: 'curses.window', screen_row: int,
                 width: int) -> None:
        self._win        = win
        self._row        = screen_row
        self._width      = width
        self.needle      = ''
        self.replacement = ''

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _clear_row(self) -> None:
        try:
            self._win.addstr(self._row, 0, ' ' * self._width,
                             curses.color_pair(_CP_DIALOG))
        except curses.error:
            pass

    def _draw_bar(self, text: str) -> None:
        self._clear_row()
        attr_hi   = curses.color_pair(_CP_DIALOG_HI) | curses.A_BOLD
        attr_norm = curses.color_pair(_CP_DIALOG)
        # Split on '|' to alternate bold/normal segments (quick markup)
        x = 0
        bold = True
        for segment in text.split('|'):
            if x >= self._width:
                break
            seg = segment[:self._width - x]
            try:
                self._win.addstr(self._row, x, seg,
                                 attr_hi if bold else attr_norm)
            except curses.error:
                pass
            x   += len(seg)
            bold = not bold

    def _read_field(self, label: str, initial: str,
                    field_w: int) -> Optional[str]:
        """Single-line input; returns string or None on Escape."""
        curses.curs_set(1)
        value = initial
        while True:
            display = value[-field_w:] if len(value) > field_w else value
            bar = (f'|{label}|[|'
                   + display.ljust(field_w)
                   + '|]  |Enter=OK  Esc=cancel')
            self._draw_bar(bar)
            cx = 1 + len(label) + 2 + min(len(value), field_w - 1)
            try:
                self._win.move(self._row, cx)
            except curses.error:
                pass
            self._win.refresh()
            try:
                key = self._win.getch()
            except KeyboardInterrupt:
                return None
            if key in (27, 3):                   # Escape or Ctrl+C = cancel
                return None
            elif key in (10, 13, curses.KEY_ENTER):
                return value
            elif key in (curses.KEY_BACKSPACE, 127, 8):
                value = value[:-1]
            elif 32 <= key <= 126:
                value += chr(key)

    # ------------------------------------------------------------------
    # Public entry points
    # ------------------------------------------------------------------

    def run_find(self, win: 'curses.window', buf: TextBuffer,
                 refresh_editor: 'Callable[[], None]') -> None:
        """Show Find bar and cycle through matches until Escape."""
        fw     = max(20, self._width // 3)
        needle = self._read_field('Find: ', self.needle, fw)
        if needle is None:
            return
        self.needle = needle
        if not needle:
            return
        pos = buf.find(needle, buf.cursor, wrap=True)
        if pos is None:
            self._draw_bar(f'|  Not found: |{needle}|  — any key')
            self._win.refresh()
            try:
                self._win.getch()
            except KeyboardInterrupt:
                pass
            return
        while pos is not None:
            r, c = pos
            buf._anchor = (r, c)
            buf.cursor  = buf._clamp(r, c + len(needle))
            refresh_editor()
            self._draw_bar(
                f'|Find: |{needle}'
                f'|  Enter=next  Esc=close  ({r+1}:{c+1})')
            self._win.refresh()
            try:
                key = self._win.getch()
            except KeyboardInterrupt:
                key = 27
            if key in (27, 3):               # Escape or Ctrl+C = close
                buf.clear_sel()
                return
            elif key in (10, 13, curses.KEY_ENTER):
                next_pos = buf.find(needle, (r, c + 1), wrap=True)
                if next_pos is None or next_pos == pos:
                    buf.clear_sel()
                    return
                pos = next_pos
            else:
                buf.clear_sel()
                return

    def run_replace(self, win: 'curses.window', buf: TextBuffer,
                    refresh_editor: 'Callable[[], None]') -> None:
        """Show Find+Replace bar."""
        fw      = max(14, (self._width - 24) // 3)
        needle  = self._read_field('Find: ', self.needle, fw)
        if needle is None:
            return
        self.needle = needle
        if not needle:
            return
        repl = self._read_field('Replace: ', self.replacement, fw)
        if repl is None:
            return
        self.replacement = repl

        pos = buf.find(needle, buf.cursor, wrap=True)
        while pos is not None:
            r, c = pos
            buf._anchor = (r, c)
            buf.cursor  = buf._clamp(r, c + len(needle))
            refresh_editor()
            self._draw_bar(
                f'|Replace? |  R=yes  A=all  Enter=skip  Esc=done'
                f'  ({r+1}:{c+1})')
            self._win.refresh()
            try:
                key = self._win.getch()
            except KeyboardInterrupt:
                key = 27
            if key in (27, 3):               # Escape or Ctrl+C = done
                buf.clear_sel()
                return
            elif key in (ord('r'), ord('R')):
                buf.commit()
                buf._delete_sel()
                buf.paste(repl)
                pos = buf.find(needle, buf.cursor, wrap=False)
            elif key in (ord('a'), ord('A')):
                buf.clear_sel()
                count = buf.replace_all(needle, repl)
                self._draw_bar(f'|Done: |{count} replacement(s)|  — any key')
                self._win.refresh()
                try:
                    self._win.getch()
                except KeyboardInterrupt:
                    pass
                return
            elif key in (10, 13, curses.KEY_ENTER):
                buf.clear_sel()
                pos = buf.find(needle, (r, c + 1), wrap=False)
            else:
                buf.clear_sel()
                return


# ---------------------------------------------------------------------------
# HelpScreen
# ---------------------------------------------------------------------------
class HelpScreen:
    """Draws a centred overlay with key-binding help.  Any key dismisses it."""

    _LINES = [
        '  Ctrl+S / F2        Save and exit       ',
        '',
        '  Arrows            Move cursor         ',
        '  Ctrl+Left/Right   Word left / right   ',
        '  Home / End        Line start / end    ',
        '  Ctrl+Home/End     Doc start / end     ',
        '  PgUp / PgDn       Page up / down      ',
        '',
        '  Shift+movement    Extend selection    ',
        '  Ctrl+A            Select all          ',
        '',
        '  Ctrl+C            Copy                ',
        '  Ctrl+X            Cut                 ',
        '  Ctrl+P            Paste               ',
        '',
        '  Ctrl+Z / Ctrl+Y   Undo / Redo         ',
        '',
        '  Ctrl+F            Find                ',
        '  Ctrl+R            Find + Replace      ',
        '',
        '  Ctrl+Delete       Delete word right   ',
        '  Insert            Toggle insert/over  ',
        '  Tab               Insert 4 spaces     ',
    ]

    @staticmethod
    def show(win: 'curses.window') -> None:
        max_h, max_w = win.getmaxyx()
        box_w = max(len(l) for l in HelpScreen._LINES) + 4
        box_h = len(HelpScreen._LINES) + 2
        box_w = min(box_w, max_w - 2)
        box_h = min(box_h, max_h - 2)
        bx = max(0, (max_w - box_w) // 2)
        by = max(0, (max_h - box_h) // 2)

        attr_border = curses.color_pair(_CP_DIALOG_HI) | curses.A_BOLD
        attr_text   = curses.color_pair(_CP_DIALOG)
        attr_title  = curses.color_pair(_CP_DIALOG_HI) | curses.A_BOLD

        try:
            win.addstr(by, bx, '+' + '-' * (box_w - 2) + '+', attr_border)
            win.addstr(by + box_h - 1, bx,
                       '+' + '-' * (box_w - 2) + '+', attr_border)
            for row in range(1, box_h - 1):
                win.addstr(by + row, bx,             '|', attr_border)
                win.addstr(by + row, bx + box_w - 1, '|', attr_border)
        except curses.error:
            pass

        for i, line in enumerate(HelpScreen._LINES[:box_h - 2]):
            attr    = attr_title if i == 0 else attr_text
            content = line[:box_w - 4].ljust(box_w - 4)
            try:
                win.addstr(by + 1 + i, bx + 2, content, attr)
            except curses.error:
                pass

        win.refresh()
        try:
            win.getch()
        except KeyboardInterrupt:
            pass


# ---------------------------------------------------------------------------
# _EditorApp  --  internal: owns the curses window and drives the editor loop
# ---------------------------------------------------------------------------
class _EditorApp:
    """
    Created by ConsoleTextEditor.editString().  Not part of the public API.

    stdscr   : the curses screen (or a sub-window for embedded use)
    buf      : TextBuffer with the initial text
    filename : display name shown in the status bar
    origin   : (screen_y, screen_x) top-left corner of the editor area
    size     : (height, width) of the editor area; None = full screen
    """

    def __init__(self, stdscr: 'curses.window', buf: TextBuffer,
                 filename: str,
                 origin: Tuple[int, int],
                 size: Optional[Tuple[int, int]],
                 use_system_clipboard: bool = False,
                 color: str = 'green') -> None:
        self._scr          = stdscr
        self._buf          = buf
        self._filename     = filename
        self._use_sys_clip = use_system_clipboard
        self._oy, self._ox = origin
        sh, sw         = stdscr.getmaxyx()
        self._h        = size[0] if size else sh
        self._w        = size[1] if size else sw

        # Row 0 = status bar; rows 1..h-2 = text area; row h-1 = find bar
        self._txt_top  = 1
        self._txt_h    = self._h - 2
        self._txt_w    = self._w - 1     # last column reserved for wrap marker

        fg_color, bg_color, bold = _COLOR_SCHEMES.get(color, _COLOR_SCHEMES['green'])
        self._bold_attr: int = curses.A_BOLD if bold else 0
        _init_colors(fg_color, bg_color)
        self._wrapper   = SoftWrapper(buf.text, self._txt_w)
        self._status    = StatusBar(stdscr, self._oy, self._w)
        self._findbar   = FindBar(stdscr, self._oy + self._h - 1, self._w)
        self._view_top  = 0
        self._flash_msg = ''    # cleared at start of next handle_key call
        # clipboard is module-level _clipboard[0]

    # ------------------------------------------------------------------
    # Layout helpers
    # ------------------------------------------------------------------

    def _text_area_rows(self) -> int:
        return max(1, self._h - 2)

    def _reflow(self) -> None:
        self._wrapper.rebuild(self._buf.text, self._txt_w)

    # ------------------------------------------------------------------
    # Scrolling / visual movement
    # ------------------------------------------------------------------

    def _scroll_to_cursor(self) -> None:
        vis_r, _ = self._wrapper.to_display(*self._buf.cursor)
        page     = self._text_area_rows()
        if vis_r < self._view_top:
            self._view_top = vis_r
        elif vis_r >= self._view_top + page:
            self._view_top = vis_r - page + 1

    def _page_up(self, extend_sel: bool = False) -> None:
        page  = self._text_area_rows()
        vis_r, vis_c = self._wrapper.to_display(*self._buf.cursor)
        new_vis_r    = max(0, vis_r - page)
        log_r, log_c = self._wrapper.to_logical(new_vis_r, vis_c)
        self._buf.set_cursor(log_r, log_c, extend_sel)
        self._view_top = max(0, self._view_top - page)

    def _page_down(self, extend_sel: bool = False) -> None:
        page  = self._text_area_rows()
        total = self._wrapper.vis_row_count()
        vis_r, vis_c = self._wrapper.to_display(*self._buf.cursor)
        new_vis_r    = min(total - 1, vis_r + page)
        log_r, log_c = self._wrapper.to_logical(new_vis_r, vis_c)
        self._buf.set_cursor(log_r, log_c, extend_sel)
        self._view_top = min(max(0, total - page), self._view_top + page)

    def _move_up_visual(self, extend_sel: bool = False) -> None:
        vis_r, vis_c = self._wrapper.to_display(*self._buf.cursor)
        if vis_r == 0:
            self._buf.move_home(extend_sel)
            return
        log_r, log_c = self._wrapper.to_logical(vis_r - 1, vis_c)
        self._buf.set_cursor(log_r, log_c, extend_sel)

    def _move_down_visual(self, extend_sel: bool = False) -> None:
        total = self._wrapper.vis_row_count()
        vis_r, vis_c = self._wrapper.to_display(*self._buf.cursor)
        if vis_r >= total - 1:
            self._buf.move_end(extend_sel)
            return
        log_r, log_c = self._wrapper.to_logical(vis_r + 1, vis_c)
        self._buf.set_cursor(log_r, log_c, extend_sel)

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------

    def _render_text_area(self) -> None:
        buf  = self._buf
        wrap = self._wrapper
        w    = self._txt_w
        page = self._text_area_rows()
        sel  = buf.sel_range()

        attr_normal = curses.color_pair(_CP_EDITOR) | self._bold_attr
        attr_sel    = curses.color_pair(_CP_SELECT)
        attr_wrap   = curses.color_pair(_CP_WRAP)   | curses.A_DIM

        for row_offset in range(page):
            vis_row = self._view_top + row_offset
            scr_row = self._oy + self._txt_top + row_offset

            try:
                self._scr.addstr(scr_row, self._ox,
                                 ' ' * (w + 1), attr_normal)
            except curses.error:
                pass

            if vis_row >= wrap.vis_row_count():
                continue

            log_row, col_off = wrap.display_rows[vis_row]
            line       = buf.text[log_row]
            segment    = line[col_off:col_off + w]
            is_wrapped = (col_off + w) < len(line)

            for ci, ch in enumerate(segment):
                log_col = col_off + ci
                in_sel  = False
                if sel:
                    (r1, c1), (r2, c2) = sel
                    if (r1, c1) <= (log_row, log_col) < (r2, c2):
                        in_sel = True
                attr = attr_sel if in_sel else attr_normal
                try:
                    self._scr.addch(scr_row, self._ox + ci, ch, attr)
                except curses.error:
                    pass

            if is_wrapped:
                try:
                    self._scr.addch(scr_row, self._ox + w,
                                    SoftWrapper.WRAP_CHAR, attr_wrap)
                except curses.error:
                    pass

    def _render_bottom_bar(self) -> None:
        """Show flash message in the find-bar row (only when find bar is closed)."""
        row = self._oy + self._h - 1
        if self._flash_msg:
            msg  = f'  {self._flash_msg}  '.ljust(self._w)[:self._w]
            attr = curses.color_pair(_CP_DIALOG_HI) | curses.A_BOLD
        else:
            msg  = ' ' * self._w
            attr = curses.color_pair(_CP_EDITOR) | self._bold_attr
        try:
            self._scr.addstr(row, self._ox, msg, attr)
        except curses.error:
            pass

    def render(self) -> None:
        self._status.render(self._filename, self._buf)
        self._render_text_area()
        self._render_bottom_bar()
        self._place_cursor()
        self._scr.refresh()

    def _place_cursor(self) -> None:
        vis_r, vis_c = self._wrapper.to_display(*self._buf.cursor)
        scr_r = self._oy + self._txt_top + (vis_r - self._view_top)
        scr_c = self._ox + vis_c
        scr_r = max(self._oy + self._txt_top,
                    min(scr_r,
                        self._oy + self._txt_top + self._text_area_rows() - 1))
        scr_c = max(self._ox, min(scr_c, self._ox + self._txt_w - 1))
        try:
            self._scr.move(scr_r, scr_c)
        except curses.error:
            pass

    # ------------------------------------------------------------------
    # Key handling
    # ------------------------------------------------------------------

    def handle_key(self, key: int) -> bool:
        """
        Process one keypress.  Returns True to keep running, False to exit.
        All mutations go through buf methods, then reflow+scroll+render.
        """
        buf = self._buf
        dirty = True   # assume text changed; set False for navigation-only
        self._flash_msg = ''   # previous flash shown; clear for this keypress

        # ---- Exit keys -----------------------------------------------
        if key in (curses.KEY_F2, 19):            # F2, Ctrl+S
            return False

        # ---- Help -------------------------------------------------------
        elif key == curses.KEY_F1:
            HelpScreen.show(self._scr)
            dirty = False

        # ---- Find / Replace --------------------------------------------
        elif key == 6:                           # Ctrl+F
            curses.curs_set(0)
            self._findbar.run_find(self._scr, buf, self.render)
            curses.curs_set(1)

        elif key == 18:                          # Ctrl+R = Find+Replace
            self._handle_ctrl_h()

        # ---- Clipboard -------------------------------------------------
        elif key == 3:                           # Ctrl+C
            text = buf.copy()
            if self._use_sys_clip:
                _sys_clip_copy(text)
            else:
                _clipboard[0] = text
            dirty = False

        elif key == 24:                          # Ctrl+X
            text = buf.cut()
            if self._use_sys_clip:
                _sys_clip_copy(text)
            else:
                _clipboard[0] = text

        elif key in (16, 22):                    # Ctrl+P or Ctrl+V = paste
            text = _sys_clip_paste() if self._use_sys_clip else _clipboard[0]
            if text:
                buf.paste(text)
            else:
                dirty = False

        # ---- Undo / Redo -----------------------------------------------
        elif key == 26:                          # Ctrl+Z
            buf.undo()

        elif key == 25:                          # Ctrl+Y
            buf.redo()

        # ---- Select all ------------------------------------------------
        elif key == 1:                           # Ctrl+A
            buf.select_all()
            dirty = False

        # ---- Insert toggle ---------------------------------------------
        elif key == curses.KEY_IC:               # Insert key
            buf.insert_mode = not buf.insert_mode
            dirty = False

        # ---- Navigation (no text change) --------------------------------
        elif key == curses.KEY_LEFT:
            buf.move_left()
            dirty = False

        elif key == curses.KEY_RIGHT:
            buf.move_right()
            dirty = False

        elif key == curses.KEY_UP:
            self._move_up_visual()
            dirty = False

        elif key == curses.KEY_DOWN:
            self._move_down_visual()
            dirty = False

        elif key == curses.KEY_HOME:
            buf.move_home()
            dirty = False

        elif key == curses.KEY_END:
            buf.move_end()
            dirty = False

        elif key == curses.KEY_PPAGE:            # Page Up
            self._page_up()
            dirty = False

        elif key == curses.KEY_NPAGE:            # Page Down
            self._page_down()
            dirty = False

        # Ctrl+Home / Ctrl+End
        # F9/F10 are the keyboard-hook redirects (WT intercepts the raw combos)
        elif key in (567, 536, curses.KEY_F9):   # Ctrl+Home
            buf.move_doc_start()
            dirty = False

        elif key in (562, 531, curses.KEY_F10):  # Ctrl+End
            buf.move_doc_end()
            dirty = False

        # Ctrl+Shift+Home / Ctrl+Shift+End  (select to document start/end)
        # Codes 568/537 are the guess for terminals that deliver these directly;
        # F11/F12 are the fallback path via the keyboard-library hook (used when
        # Windows Terminal intercepts the raw Ctrl+Shift+Home/End events).
        elif key in (568, 537, curses.KEY_F8):   # Ctrl+Shift+Home
            buf.move_doc_start(extend_sel=True)
            dirty = False

        elif key in (563, 532, curses.KEY_F12):  # Ctrl+Shift+End
            buf.move_doc_end(extend_sel=True)
            dirty = False

        # Ctrl+Left / Ctrl+Right  (xterm: 545/544, Windows console: 443)
        elif key in (545, 544, 443):
            buf.move_word_left()
            dirty = False

        elif key in (560, 559, 444):
            buf.move_word_right()
            dirty = False

        # Ctrl+Shift+Left / Ctrl+Shift+Right  (xterm: 546/561)
        elif key in (546,):
            buf.move_word_left(extend_sel=True)
            dirty = False

        elif key in (561,):
            buf.move_word_right(extend_sel=True)
            dirty = False

        # ---- Shift+navigation (extend selection) -----------------------
        elif key == curses.KEY_SLEFT:
            buf.move_left(extend_sel=True)
            dirty = False

        elif key == curses.KEY_SRIGHT:
            buf.move_right(extend_sel=True)
            dirty = False

        elif key in (curses.KEY_SR, 337, 393, 547):  # Shift+Up
            self._move_up_visual(extend_sel=True)
            dirty = False

        elif key in (curses.KEY_SF, 336, 400, 548):  # Shift+Down
            self._move_down_visual(extend_sel=True)
            dirty = False

        elif key == curses.KEY_SHOME:
            buf.move_home(extend_sel=True)
            dirty = False

        elif key == curses.KEY_SEND:
            buf.move_end(extend_sel=True)
            dirty = False

        elif key == curses.KEY_SPREVIOUS:        # Shift+PgUp
            self._page_up(extend_sel=True)
            dirty = False

        elif key == curses.KEY_SNEXT:            # Shift+PgDn
            self._page_down(extend_sel=True)
            dirty = False

        # ---- Editing ---------------------------------------------------
        elif key in (10, 13, curses.KEY_ENTER):  # Enter
            buf.insert_newline()

        elif key in (curses.KEY_BACKSPACE, 127, 8): # Backspace (8 = Windows console)
            buf.backspace()

        elif key == curses.KEY_DC:               # Delete
            buf.delete_char()

        elif key == 9:                           # Tab -> 4 spaces
            for _ in range(4):
                buf.insert_char(' ')

        # Ctrl+Delete
        elif key == curses.KEY_SDC or key == 519:
            buf.delete_word_right()

        # ---- Printable characters --------------------------------------
        elif 32 <= key <= 126:
            buf.insert_char(chr(key))

        else:
            dirty = False   # unknown key: no change

        if dirty:
            self._reflow()
        self._scroll_to_cursor()
        self.render()
        return True

    def _handle_ctrl_h(self) -> None:
        """Explicit entry point for Find+Replace (called when we can distinguish it)."""
        curses.curs_set(0)
        self._findbar.run_replace(self._scr, self._buf, self.render)
        curses.curs_set(1)

    # ------------------------------------------------------------------
    # Bracketed-paste interception
    # ------------------------------------------------------------------

    def _drain_bracketed_paste(self) -> str:
        """Consume input until ESC[201~ and return the captured text."""
        # ESC [ 2 0 1 ~  →  27 91 50 48 49 126
        end      = [27, 91, 50, 48, 49, 126]
        captured: List[int] = []
        recent:   List[int] = []
        self._scr.timeout(500)
        try:
            while True:
                c = self._scr.getch()
                if c == -1:
                    break               # timeout — stop collecting
                captured.append(c)
                recent.append(c)
                if len(recent) > 6:
                    recent.pop(0)
                if recent == end:
                    del captured[-6:]   # strip end marker bytes
                    break
        finally:
            self._scr.timeout(-1)
        # Convert to text; accept printable chars, tabs, and newlines
        chars = []
        for code in captured:
            try:
                ch = chr(code)
                if ch.isprintable() or ch in '\r\n\t':
                    chars.append('\n' if ch == '\r' else ch)
            except (ValueError, OverflowError):
                pass
        return ''.join(chars)

    def _handle_esc(self) -> None:
        """
        Called when the main loop receives ESC (27).

        Peeks at the characters that follow (with a short timeout).  If they
        form the bracketed-paste start marker ESC[200~, drains the system-
        clipboard content that Windows Terminal injected and performs an
        internal paste from _clipboard[0] instead.

        Any other ESC sequence (or lone ESC) is silently ignored.
        """
        # Read up to 5 characters with a short timeout.  By the time curses
        # delivers 27 to us it has already waited ESCDELAY ms, so the rest of
        # the sequence is already buffered and arrives instantly.
        self._scr.timeout(50)
        seq: List[int] = []
        try:
            while len(seq) < 5:
                c = self._scr.getch()
                if c == -1:
                    break
                seq.append(c)
        finally:
            self._scr.timeout(-1)

        # ESC [ 2 0 0 ~  →  bracketed paste start
        if seq == [91, 50, 48, 48, 126]:
            pasted = self._drain_bracketed_paste()
            # system-clipboard mode: use the text the terminal injected;
            # internal-clipboard mode: discard injected text, use _clipboard[0].
            text = pasted if self._use_sys_clip else _clipboard[0]
            if text:
                self._buf.paste(text)
                self._reflow()
            self._scroll_to_cursor()
            self.render()
        # else: lone ESC or some other escape sequence — ignore

    # ------------------------------------------------------------------
    # Main loop
    # ------------------------------------------------------------------

    def run(self) -> str:
        """Run the editor event loop; return the final text."""
        curses.curs_set(1)
        self._scr.keypad(True)
        self._scr.timeout(-1)        # blocking reads
        # raw() passes Ctrl+C as key 3 instead of generating SIGINT
        curses.raw()
        # Honour the short ESCDELAY set by editString(); fall back to the
        # curses Python API if the env-var approach is not enough.
        try:
            curses.set_escdelay(25)
        except AttributeError:
            pass
        try:
            self.render()
            while True:
                try:
                    key = self._scr.getch()
                except KeyboardInterrupt:
                    key = 3  # fallback if raw() doesn't suppress SIGINT

                # Terminal resize
                if key == curses.KEY_RESIZE:
                    sh, sw = self._scr.getmaxyx()
                    self._h    = sh
                    self._w    = sw
                    self._txt_w = sw - 1
                    self._status._width  = sw
                    self._findbar._width = sw
                    self._findbar._row   = self._oy + sh - 1
                    self._reflow()
                    self._scroll_to_cursor()
                    self.render()
                    continue

                # ESC (27) — may be bracketed-paste start (ESC[200~) sent by the
                # terminal when the user presses Ctrl+V.  Intercept it here before
                # handle_key sees it so we can drain the system-clipboard content
                # and substitute our internal clipboard instead.
                if key == 27:
                    self._handle_esc()
                    continue

                running = self.handle_key(key)
                if not running:
                    break
        finally:
            curses.noraw()
            curses.cbreak()

        curses.curs_set(0)
        return self._buf.get_text()


# ---------------------------------------------------------------------------
# System clipboard helpers  (used when use_system_clipboard is True)
# Requires:  pip install pyperclip
# ---------------------------------------------------------------------------

def _sys_clip_copy(text: str) -> None:
    """Write *text* to the OS clipboard via pyperclip (no-op if unavailable)."""
    try:
        import pyperclip  # type: ignore[import]
        pyperclip.copy(text)
    except Exception:
        pass


def _sys_clip_paste() -> str:
    """Read text from the OS clipboard via pyperclip; returns '' on failure."""
    try:
        import pyperclip  # type: ignore[import]
        return pyperclip.paste() or ''
    except Exception:
        return ''


# ---------------------------------------------------------------------------
# Ctrl+V keyboard hook (Windows — requires 'pip install keyboard')
# ---------------------------------------------------------------------------

def _try_hook_ctrl_v(hook_ctrl_v: bool = True) -> Optional[object]:
    """
    Install OS-level keyboard hooks that intercept keys Windows Terminal
    would otherwise consume before they reach our curses app.

    hook_ctrl_v=True  (default): also remap Ctrl+V → Ctrl+P so the internal
                                 clipboard is used instead of the system one.
    hook_ctrl_v=False: skip the Ctrl+V remap (caller wants system clipboard).

    Ctrl+Shift+End  → F12   (select to doc end;  WT consumes the raw combo)
    Ctrl+Shift+Home → F11   (select to doc start; WT consumes the raw combo)

    Uses the 'keyboard' library (pip install keyboard).  Returns None if not
    installed; bracketed-paste fallback handles Ctrl+V in that case and the
    Ctrl+Shift+Home/End select bindings simply won't be available.
    """
    try:
        import keyboard as _kb  # type: ignore[import]
        if hook_ctrl_v:
            _kb.add_hotkey('ctrl+v', lambda: _kb.send('ctrl+p'), suppress=True)
        _kb.add_hotkey('ctrl+end',        lambda: _kb.send('f10'), suppress=True)
        _kb.add_hotkey('ctrl+home',       lambda: _kb.send('f9'),  suppress=True)
        _kb.add_hotkey('ctrl+shift+end',  lambda: _kb.send('f12'), suppress=True)
        _kb.add_hotkey('ctrl+shift+home', lambda: _kb.send('f8'),  suppress=True)
        return _kb
    except Exception:
        return None


def _unhook_ctrl_v(kb: Optional[object], hook_ctrl_v: bool = True) -> None:
    """Remove the keyboard hooks installed by _try_hook_ctrl_v()."""
    if kb is not None:
        combos = ['ctrl+end', 'ctrl+home', 'ctrl+shift+end', 'ctrl+shift+home']
        if hook_ctrl_v:
            combos.insert(0, 'ctrl+v')
        for combo in combos:
            try:
                kb.remove_hotkey(combo)  # type: ignore[union-attr]
            except Exception:
                pass


# ---------------------------------------------------------------------------
# ConsoleTextEditor  —  public API class
# ---------------------------------------------------------------------------
class ConsoleTextEditor:
    """
    Reusable curses text editor widget.

    Parameters (all optional):

        string   : initial text content; None = start empty
        filename : label shown in the status bar (no file I/O performed)
        x, y     : top-left corner of the editor window in screen cells
        w, h     : width / height of the editor window
                   Any of x/y/w/h = None means use the full terminal dimension.
        color    : color scheme name (default 'green').
                   Black-bg: 'green' | 'amber' | 'white' | 'yellow'
                   Blue-bg:  'green-blue' | 'amber-blue' | 'white-blue' | 'yellow-blue'

    Usage::

        ed = ConsoleTextEditor("hello world", filename="notes.txt")
        result = ed.editString()

        # Adjust geometry before showing:
        ed.x, ed.y, ed.w, ed.h = 5, 2, 60, 20
        result = ed.editString()

        # Or supply a new string at call time:
        result = ed.editString("different text")

        # Override color per call:
        result = ed.editString(color='green-blue')
    """

    def __init__(self, string: Optional[str] = None, *,
                 filename: Optional[str] = None,
                 x: Optional[int] = None,
                 y: Optional[int] = None,
                 w: Optional[int] = None,
                 h: Optional[int] = None,
                 color: str = 'green') -> None:
        self._string   = string or ''
        self._filename = filename or ''
        self._x = x
        self._y = y
        self._w = w
        self._h = h
        self.use_system_clipboard: bool = False
        self.color: str = color

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def clipboard(self) -> str:
        """Shared clipboard text.  Set before editString() to pre-load paste content."""
        return _clipboard[0]

    @clipboard.setter
    def clipboard(self, value: str) -> None:
        _clipboard[0] = value

    @property
    def x(self) -> Optional[int]:
        return self._x

    @x.setter
    def x(self, value: Optional[int]) -> None:
        self._x = value

    @property
    def y(self) -> Optional[int]:
        return self._y

    @y.setter
    def y(self, value: Optional[int]) -> None:
        self._y = value

    @property
    def w(self) -> Optional[int]:
        return self._w

    @w.setter
    def w(self, value: Optional[int]) -> None:
        self._w = value

    @property
    def h(self) -> Optional[int]:
        return self._h

    @h.setter
    def h(self, value: Optional[int]) -> None:
        self._h = value

    @property
    def filename(self) -> str:
        return self._filename

    @filename.setter
    def filename(self, value: str) -> None:
        self._filename = value or ''

    # ------------------------------------------------------------------
    # Main entry point
    # ------------------------------------------------------------------

    def editString(self, string: Optional[str] = None, *,
                   color: Optional[str] = None) -> str:
        """
        Open the curses editor and return the edited text.

        If *string* is provided it overrides the text set in the constructor.
        If *color* is provided it overrides the color scheme set in the constructor.
        x / y / w / h == None means use the full terminal dimensions.
        """
        text         = string if string is not None else self._string
        use_sys_clip = self.use_system_clipboard
        color        = color if color is not None else self.color

        result: List[str] = ['']   # mutable container for the closure

        def _run(stdscr: 'curses.window') -> None:
            stdscr.clear()   # erase any background content (e.g. parent app's curses UI)
            sh, sw   = stdscr.getmaxyx()
            oy       = self._y if self._y is not None else 0
            ox       = self._x if self._x is not None else 0
            height   = self._h if self._h is not None else sh
            width    = self._w if self._w is not None else sw
            # Clamp to screen
            height   = min(height, sh - oy)
            width    = min(width,  sw - ox)
            buf      = TextBuffer(text)
            app      = _EditorApp(stdscr, buf, self._filename,
                                  (oy, ox), (height, width),
                                  use_system_clipboard=use_sys_clip,
                                  color=color)
            result[0] = app.run()

        # Short escape-sequence timeout so ESC is detected without a 1-second delay.
        # Must be set before curses.initscr() (i.e. before curses.wrapper).
        os.environ.setdefault('ESCDELAY', '25')

        _old_sigint = signal.signal(signal.SIGINT, signal.SIG_IGN)
        # When use_system_clipboard is False (default): remap Ctrl+V → Ctrl+P
        # so Windows Terminal's clipboard injection is bypassed in favour of the
        # internal clipboard.  When True: leave Ctrl+V alone so WT injects the
        # system clipboard content via bracketed paste, which _handle_esc() captures.
        _kb = _try_hook_ctrl_v(hook_ctrl_v=not use_sys_clip)
        # Bracketed paste mode: terminal wraps Ctrl+V injections in ESC[200~…ESC[201~.
        # Always enabled — used to capture content when use_system_clipboard=True,
        # or to drain/discard WT's injection when use_system_clipboard=False.
        sys.stdout.write('\033[?2004h')
        sys.stdout.flush()
        try:
            curses.wrapper(_run)
        finally:
            signal.signal(signal.SIGINT, _old_sigint)
            _unhook_ctrl_v(_kb, hook_ctrl_v=not use_sys_clip)
            sys.stdout.write('\033[?2004l')
            sys.stdout.flush()
        return result[0]


# ---------------------------------------------------------------------------
# Module-level convenience function
# ---------------------------------------------------------------------------
def editString(string: Optional[str] = None, *,
               filename: Optional[str] = None,
               x: Optional[int] = None,
               y: Optional[int] = None,
               w: Optional[int] = None,
               h: Optional[int] = None,
               color: str = 'green') -> str:
    """
    Open a curses text editor and return the edited string.

    Convenience wrapper — creates a ConsoleTextEditor and calls editString().

        result = editString("hello world", filename="notes.txt")
        result = editString("hello", color='green-blue')
    """
    return ConsoleTextEditor(
        string, filename=filename, x=x, y=y, w=w, h=h, color=color
    ).editString()


# ---------------------------------------------------------------------------
# Standalone utility  (python pytextedit.py [filename])
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    import os

    if len(sys.argv) > 1:
        path = sys.argv[1]
        try:
            with open(path, 'r', encoding='utf-8') as fh:
                initial = fh.read()
        except FileNotFoundError:
            initial = ''
        except OSError as exc:
            print(f'pytextedit: cannot open {path!r}: {exc}', file=sys.stderr)
            sys.exit(1)

        result = editString(initial, filename=os.path.basename(path))

        try:
            with open(path, 'w', encoding='utf-8') as fh:
                fh.write(result)
        except OSError as exc:
            print(f'pytextedit: cannot write {path!r}: {exc}', file=sys.stderr)
            sys.exit(1)
    else:
        # No filename: edit empty buffer, write result to stdout
        result = editString(filename='[stdin]')
        sys.stdout.write(result)
        if result and not result.endswith('\n'):
            sys.stdout.write('\n')
