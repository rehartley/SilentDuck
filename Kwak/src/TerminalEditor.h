// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#pragma once

// TerminalEditor -- the real "EDITOR" sentinel replacement for the console
// tool, using a curses-family library (PDCursesMod on Windows, the system's
// real ncursesw on Linux/macOS -- see the Makefile's backend selection; this
// source file itself is identical on both).
//
// This exists instead of a GUI dialog for concrete, load-bearing reasons,
// not nostalgia -- see kwak_design.md (carried forward from Quacque's
// quacque_design.md, which first made this case):
//   - portability to embedded/headless targets with no windowing system
//   - usable over a plain remote shell (SSH/telnet), which a GUI dialog
//     fundamentally cannot be
//   - a narrower, different attack surface than a GUI widget in an
//     environment where Win32 GUI-call interception (keyloggers, widget/
//     window-message hooking, screen scraping) is a real threat -- console
//     I/O over an already-encrypted remote shell session doesn't hand an
//     adversary a text-edit control to hook
//
// Same functional contract as Quacque's TerminalEditor: input mode returns
// whatever text was typed, or std::nullopt on cancel; display mode shows
// text read-only and returns nothing. Feature scope is intentionally the
// same too -- navigation, insert/delete, save/cancel, undo/redo -- not a
// port of pytextedit.py's find-replace machinery, since otp.py's own usage
// never exercises any of that.

#include <optional>
#include <string>

class TerminalEditor
{
public:
    // allowedChars mirrors otp.py's editString(allowed_chars=...): empty
    // (default) = no restriction; otherwise only characters in allowedChars
    // (matched case-insensitively) can be typed, and a disallowed keystroke
    // gets a beep() instead. F5 pastes allowedChars itself into the buffer,
    // for anyone who can't recall how to type one of its more exotic
    // characters -- documented in the F1 help overlay. OtpCli.cpp passes
    // OTP::allowedInputChars() explicitly at its call sites; this class
    // itself knows nothing about OTP.

    // Input mode: full-screen, empty buffer. F2 or Ctrl+S saves and returns
    // the buffer's text; ESC cancels and returns std::nullopt.
    static std::optional<std::u32string> getText(const std::u32string &title,
                                                   const std::u32string &allowedChars = std::u32string());

    // Display mode: full-screen, read-only, pre-filled with initialText.
    // Any key closes it. Never touches the filesystem. allowedChars has no
    // effect in this mode (read-only blocks all typed input) but is
    // accepted for call-site symmetry with getText().
    static void showText(const std::u32string &title, const std::u32string &initialText,
                          const std::u32string &allowedChars = std::u32string());
};
