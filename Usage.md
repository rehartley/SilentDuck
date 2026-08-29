# SilentDuck OTP

This repository contains `otp.py`, a Python implementation of a one-time pad (OTP) automation tool called "SILENT DUCK". It automates the manual paper-pad procedure used historically for OTP encryption (in the style of "TRIGON"-type systems), letting text be converted to/from digit streams and combined with random keypads generated from OS entropy. There is also a small wrapper script called `otp_wrapper.py` to support building a standalone executable with PyInstaller without modifying the original `otp.py`.

## Files

- `otp.py` - Main OTP program source file. It provides command-line options for key generation, encryption, decryption, message splitting, key combining, wiping, and related OTP operations.
- `pytextedit.py` - Reusable, dependency-free (aside from `curses`) full-screen console text editor. `otp.py` uses it to implement the `EDITOR` pseudo-filename described below.
- `otp_wrapper.py` - Simple wrapper entrypoint that imports `otp` and calls `otp.main()`. This file is used to build a standalone executable with PyInstaller.
- `otp_wrapper.spec` - PyInstaller spec file generated during the executable build process.
- `otp_test/` - Sample input files, generated keypads, and `test_otp.bat`, a batch script that exercises most of the CLI options end-to-end (key generation, encipher/decipher, key derivation, join/unjoin, split/merge, stream combine, wipe).

## Features

- **Text-to-digit encoding** via a straddling checkerboard: converts Latin and Cyrillic letters, digits, and a small set of punctuation into a stream of decimal digits (and back), so a classic paper one-time pad can be applied to it. The most common letters use a single digit; less common letters use two digits.
- **Dual alphabet support**: Latin and Cyrillic text can be mixed in one message; a `~` character marks a switch between alphabets, and a `#` character marks the start/end of a literal digit run.
- **Optional "Morse-cut" shorthand** (`-z`): maps 8 of the 10 digits to letters that are easier to key by hand in Morse code (`T A U V 4 E 6 B D N`, with `4` and `6` left as-is), for transcription of ciphertext.
- **Keypad generation** (`-g`): generates OTP keypads from `os.urandom`, laid out as 25-page pads matching a physical paper pad (5-digit groups, 5 groups per line, 10 lines per page = 250 digits/page).
- **Encipher / decipher** (`-e` / `-d`): encode a text file and combine it (digit-wise, mod 10) with one or more keypad files; keys can span multiple files/pages.
- **Known-plaintext key derivation** (`-f`): given a plaintext and its matching ciphertext, derive the key that would produce that pairing.
- **Two-key combine / recover** (`-j` / `-u`): merges two existing OTP sheets into a brand-new 25-sheet keypad using a combinatorial scheme (`combo5x10`, 252 combinations of 5-of-10 row sums) — a way to re-key without a fresh face-to-face exchange, by using two sheets from pads both parties already hold.
- **Secret-splitting** (`-s` / `-m`): splits a message into N shares (via random-digit differencing) across every combination of a minimum threshold, so that any `min`-of-`max` couriers can reconstruct the message but fewer cannot.
- **Stream combination** (`-b`): combines two digit streams together (mod-10 addition) to merge/whiten entropy from multiple sources, optionally repackaging the result into keypad-sized files.
- **Secure file wiping**: input files and used key files are overwritten in multiple passes (alternating `0xFF`/`0x00`/hex patterns, configurable round count) and deleted after use, so plaintext and spent keys don't linger on disk.
- **Testing mode** (`-t`): dry-run switch that suppresses all file writes/deletes, useful for verifying behavior without touching the filesystem.
- **Screen-only input/output via `EDITOR`**: passing the literal filename `EDITOR` to any file argument (`-i`, `-o`, `-c`, `-p`, `-a`, `-y`, or a key filename) opens a built-in full-screen text editor (see `pytextedit.py`) instead of reading/writing a file. As an input, you type or paste the text and it's used directly; as an output, the result (e.g. deciphered plaintext) is shown on screen and never written to disk. `EDITOR` is also skipped by the automatic file-wiping step, since there's no file on disk to wipe. This lets you encipher/decipher messages without plaintext ever touching the file system.

## Command-line options

| Flag | Arguments | Description |
|------|-----------|-------------|
| `-v` | | Print version number and exit. |
| `-h` | | Print brief help and exit. |
| `-hh` | | Print full/verbose help and exit. |
| `-z` | | Enable Morse-cut digit/letter shorthand for encode and decode. |
| `-t` | | Testing mode: suppress all file writes/deletes (dry run). |
| `-k` | | Keep input and key files after use instead of securely wiping them. |
| `-n` | `seconds` | Sleep this many seconds after every 25 random digits fetched, to throttle entropy consumption. |
| `-r` | `rounds` | Number of overwrite passes used when wiping files (default 7). |
| `-q` | `count` | Number of extra random draws added together when generating each key page (whitening); must be > 0. |
| `-g` | `-y prefix` | Generate a new 25-page OTP keypad, written to `prefix-001.otk` … `prefix-025.otk`. |
| `-e` | `-i in -o out KEYFILE...` | Encipher: encode `in`, subtract key digits, write code groups to `out`; wipes `in` and the key files afterward (unless `-k`). |
| `-d` | `-i in -o out KEYFILE...` | Decipher: add key digits to `in`, decode back to letters, write to `out`; wipes `in` and the key files afterward (unless `-k`). |
| `-f` | `-c ciphertext -p plaintext -y keyfile` | Derive a key file that makes `plaintext`'s encoding match the given `ciphertext`. |
| `-j` | `-i key1 -a key2 -o combined -y prefix` | Combine two OTP sheets into a new 25-sheet keypad: writes an encrypted `combined` file (to send to the recipient) and the new cleartext keypad (`prefix##.otk`) to use locally. |
| `-u` | `-i key1 -a key2 -c combined -y prefix` | Recover a keypad on the recipient's side from their two sheets and the received `combined` file; wipes `key1`/`key2` afterward (unless `-k`). |
| `-s` | `-i in -l min -x max PREFIX` | Split a message into secret-shares such that any `min` of `max` shares reconstruct it; writes one file per share/combination under `PREFIX`; wipes `in` afterward (unless `-k`). |
| `-m` | `-o out FILE...` | Merge previously split shares back into the original message. |
| `-b` | `-i in1 -a in2 [-c combined] [-y prefix]` | Combine two digit streams (mod-10 addition); optionally writes the combined stream and/or splits it into keypad-sized files under `prefix`; wipes `in1`/`in2` afterward (unless `-k`). |
| `-w` | `FILE...` | Securely wipe (multi-pass overwrite + delete) the given files. |

Only one command option (`-v -h -hh -d -e -f -g -j -u -s -m -b -w`) is acted on per invocation. `-z -t -k -n -q -r` are modifiers that can be combined with any command. Unrecognized/positional arguments (such as key filenames) are collected as extra arguments for the active command.

Any file argument above (`-i`, `-o`, `-c`, `-p`, `-a`, `-y`, or a bare key filename) accepts the special value `EDITOR` in place of a real path, to read or display that text on screen instead of touching the file system — see **Screen-only input/output via `EDITOR`** above. For example:

```powershell
# type the plaintext on screen instead of reading a file
otp -e -i EDITOR -o outputfile.otp keys\XX123-01.otk

# view the deciphered plaintext on screen instead of writing a file
otp -d -i outputfile.otp -o EDITOR keys\XX123-01.otk
```

## Usage

### Run directly with Python

```powershell
python otp.py [options]
```

### Run via wrapper script

```powershell
python otp_wrapper.py [options]
```

### Run the standalone executable

After building with PyInstaller, the executable is available at:

```text
dist/otp_wrapper.exe
```

Then run:

```powershell
.\dist\otp_wrapper.exe [options]
```

See `otp_test/test_otp.bat` for a worked example exercising most commands in sequence.

## Build

The wrapper was created so `otp.py` can remain unchanged while producing a standalone Windows executable with PyInstaller.

A build command used for this project is:

```powershell
python -m PyInstaller --onefile otp_wrapper.py
```

## Notes

- `otp_wrapper.py` is intentionally minimal and only forwards execution to `otp.main()`.
- The original OTP implementation remains in `otp.py`, preserving its source format and CLI behavior.
- `otp.py` has been developed on and off since 2019 and is released to the public under the Creative Commons license.