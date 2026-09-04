# SilentDuck

One time pad (OTP) encryption, automating a historical Cold War-era
pencil-and-paper procedure, plus an *experimental* modern rekeying scheme so
distant operators never run out of pads.

*(This experimental combinatorial key-sharing/rekeying mechanism's potential cryptanalytic security is still being examined.  In all likelihood, this part of the project may be bumping up against ["Schneier's Law"](https://www.schneier.com/blog/archives/2011/04/schneiers_law.html), but so far is holding fast..)*

<details>
<summary><strong>Table of Contents</strong></summary>

- [Before you start](#before-you-start)
- [Quick start](#quick-start)
- [Implementations](#implementations)
- [How it works](#how-it-works)
- [Message format](#message-format)
- [Documentation](#documentation)

</details>

Before you start
----------------

**Read this before you use it for anything real.** This tool wipes and
deletes input files (including key files) after every command by default.
If you generate keys, don't keep a copy elsewhere, and then encipher
something, the keys and plaintext are gone — OTP only works if you keep
your own copy of the key material safe. Use `-t` (test mode, no file
writes) or `-k` (keep files, skip the wipe) while you're learning.

Quick start
-----------

Requires Python 3.9+. Run straight from source, no install needed:

```bash
python otp.py -h              # help
python otp.py -g -y keys/XX   # generate a 25-page keypad: keys/XX-001.otk ... XX-025.otk
python otp.py -e -i in.txt -o out.otp keys/XX-001.otk   # encipher
python otp.py -d -i out.otp -o out.txt keys/XX-001.otk  # decipher
```

To build a standalone executable that needs no Python installed, run
`buildit.bat` (or `python -m PyInstaller --onefile otp_wrapper.py`
directly) — see [Usage.md](Usage.md) for the full build notes.

For every other flag (`-j`/`-u` rekeying, `-s`/`-m` secret-splitting, `-b`
stream combining, `-z` Morse-cut shorthand, entropy throttling, wipe
rounds, the on-screen `EDITOR` pseudo-file, ...), see
[Usage.md](Usage.md)'s full option table, or work through
[Silent Duck Manual.md](Silent%20Duck%20Manual.md) one command at a time.

Implementations
----------------

The same OTP logic exists in a few forms, each its own project:

- **`otp.py`** — the original, this repository's reference implementation.
  Pure Python, no dependencies beyond the standard library.
- **[Quacque](Quacque/quacque_design.md)** — a native Qt/C++ port of the
  same engine, built as a standalone console tool.

How it works
-------------

SILENT DUCK operates on Roman and Cyrillic letters, digits, and a small set
of punctuation:

```
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ы Ь Э Ю Я
0 1 2 3 4 5 6 7 8 9
? : @ / ~ # .
```

(space and newline are included but hard to show above). Missing from the
Cyrillic set are "Ё" and "Ъ" (the hard sign).

Text is converted to digits with a "straddling checkerboard": the seven
most common letters become single digits `0`-`6`, everything else becomes
two digits `70`-`99`. A `~` marks a switch between Latin/Cyrillic, and `#`
marks the start/end of a literal run of digits. The digit string is then
combined (mod 10, no carrying) with a page of pre-shared random key digits
— subtracting to encipher, adding to decipher — and the key page is
destroyed once used.

For the full worked example (straddling checkerboard tables, encoding a
mixed Latin/Cyrillic message by hand, Morse-cut shorthand, and the
pen-and-paper procedure alongside the equivalent `otp` command for every
feature), see [Silent Duck Manual.md](Silent%20Duck%20Manual.md). History
and pictures of the real paper artifacts this is modeled on:
[The Alexandr Ogorodnik (TRIGON) Case](https://web.archive.org/web/20260821182405/https://www.numbers-stations.com/articles/trigon-numbers-station-the-case-of-alexandr-ogorodnik/).

If curious about the experimental rekeying scheme, the design write-ups are
in [Documentation](#documentation) below, and outside review is being
sought [here](https://crypto.stackexchange.com/questions/119883/work-factor-of-a-two-pad-sorted-combinatorial-key-wrapping-scheme-for-manual-ot).


Message format
---------------

Number station traffic is commonly composed of fields such as: a one-digit
message type, a one-digit region code, the recipient's three-digit station
code, a three-digit code-group count, a five-digit key identifier, the
five-digit code groups themselves, and "00000" sent three times to end the
transmission. Message types typically seen: `0` null, `1` normal, `2`
retransmission, `3` test, `4` key generation, `5` key compromise, `6`
special announcement, `7` relay, `8` bulk data, `9` super-encrypted.

Documentation
--------------

- [Usage.md](Usage.md) — full reference for `otp.py`'s files, features,
  and command-line options.
- [Silent Duck Manual.md](Silent%20Duck%20Manual.md) — a lesson-by-lesson
  walkthrough of every SD command, each covering the `otp` command line
  and, where a manual equivalent exists, the pen-and-paper version.
- [key_exchange_instructions.md](key_exchange_instructions.md) — a
  standalone, pencil-and-paper procedure for turning two old key sheets
  into one brand-new keypad and getting it safely to the other party,
  without a computer.
- [SilentDuck_rekeying_as_a_KDF.md](SilentDuck_rekeying_as_a_KDF.md) — the
  conceptual case for the rekeying scheme: the shared public artifact is a
  synchronization pointer (HKDF-style salt/nonce), not an entropy source,
  so Shannon's perfect-secrecy bound doesn't apply; covers the three
  correctness conditions and the operational payoff (no courier,
  per-period forward secrecy, deniability at every layer).
- [combinometrics_manual_otp.md](combinometrics_manual_otp.md) — a
  hand-worked, by-hand-checkable walkthrough of the `-j`/`-u` combinatorial
  key-expansion math, companion to `combinometrics_analysis.py`.
- [clear_text_attack_analysis.md](clear_text_attack_analysis.md) — whether
  a later plaintext leak on some messages compromises others encrypted
  under keys delivered via `-j`/`-u`. Companion demo:
  [keysharing_attack_demo.py](keysharing_attack_demo.py).
- [broadcast_rekeying_analysis.md](broadcast_rekeying_analysis.md) — the
  operational case for a broadcast-not-addressed rekeying network, and its
  caveats. Companion demo:
  [broadcast_rekeying_demo.py](broadcast_rekeying_demo.py).
- [otp-rekey-post.md](otp-rekey-post.md) — the question posted to
  Cryptography Stack Exchange asking outside cryptographers to check the
  `-j`/`-u` construction's work factor; see also the
  [discussion thread](https://crypto.stackexchange.com/questions/119883/work-factor-of-a-two-pad-sorted-combinatorial-key-wrapping-scheme-for-manual-ot).
