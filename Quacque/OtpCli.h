#pragma once

// otp_main() -- console entry point functionally equivalent to otp.py's CLI:
// same flags, same file-argument semantics, same "EDITOR" sentinel behavior.
// Built entirely on OTP's public API -- the same surface a future GUI will
// use -- so this doubles as an end-to-end exercise of that API, not just a
// convenience tool. See ../quacque_design.md.
//
// Requires a QApplication to already exist (EditorDialog's modal dialogs
// need an event loop) -- see main.cpp.

int otp_main(int argc, char *argv[]);
