// ########################################### //
// Released under the BSD Zero Clause License  //
// ########################################### //
//
// Released to the public domain: use, copy, modify, and distribute this
// code for any purpose, commercial or otherwise, with or without fee or
// attribution -- no restrictions, anywhere.

#pragma once

// otp_main() -- console entry point functionally equivalent to otp.py's (and
// Quacque's) CLI: same flags, same file-argument semantics, same "EDITOR"
// sentinel behavior. Built entirely on OTP's public API. See ../kwak_design.md.

int otp_main(int argc, char *argv[]);
