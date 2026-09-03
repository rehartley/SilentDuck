#pragma once

// TerminalEditor -- the real "EDITOR" sentinel replacement for the console
// tool, using PDCursesMod (the same category of library pytextedit.py
// itself depends on via Python's curses/windows-curses).
//
// This exists instead of a GUI dialog for concrete, load-bearing reasons,
// not nostalgia -- see quacque_design.md:
//   - portability to embedded/headless targets with no windowing system
//   - usable over a plain remote shell (SSH/telnet), which a GUI dialog
//     fundamentally cannot be
//   - a narrower, different attack surface than a GUI widget in an
//     environment where Win32 GUI-call interception (keyloggers, widget/
//     window-message hooking, screen scraping) is a real threat -- console
//     I/O over an already-encrypted remote shell session doesn't hand an
//     adversary a text-edit control to hook
//
// Qt's EditorDialog (Editor.h/.cpp) is NOT used by the console tool for
// exactly these reasons; it's kept in the tree for the future GUI app,
// where a window is the actually-correct interface. See OtpCli.cpp for
// which one gets used.
//
// Same functional contract as EditorDialog: input mode returns whatever
// text was typed (or a null QString on cancel); display mode shows text
// read-only and returns nothing. Feature scope is intentionally the same
// too -- navigation, insert/delete, save/cancel -- not a port of
// pytextedit.py's undo/redo/selection/find-replace machinery, since
// otp.py's own usage never exercises any of that. What changed from the
// earlier (wrong) design isn't the feature scope, it's the runtime
// environment: a real terminal, not a GUI window.

#include <QString>

class TerminalEditor
{
public:
    // Input mode: full-screen, empty buffer. F2 or Ctrl+S saves and
    // returns the buffer's text; ESC cancels and returns a null QString
    // (.isNull() == true).
    static QString getText(const QString &title);

    // Display mode: full-screen, read-only, pre-filled with initialText.
    // Any key closes it. Never touches the filesystem, matching otp.py's
    // EDITOR-as-output-argument behavior exactly.
    static void showText(const QString &title, const QString &initialText);
};
