# Pad-at-rest encryption: a crypto-shredding design for key storage

## Status

This is a **design proposal, not implemented code**. Nothing in `otp.py`,
`Quacque`, `QuacqueGUI`, or `Yaya` does any of this yet. It's written up
here because it's substantial enough to want a record independent of any
one client, and because it directly answers a gap flagged in
[clear_text_attack_analysis.md](clear_text_attack_analysis.md)'s
"Addendum 2."

## The problem this addresses

Today, `otp.py` stores pad/key material as plaintext digit sheets on disk
(the `.otk` files under `keys/`), and "destroying" a used sheet means
`wipeFile()` — a multi-pass overwrite ([otp.py:1707-1752](otp.py#L1707-L1752)).
Addendum 2 of `clear_text_attack_analysis.md` lays out why that overwrite
is not a reliable physical guarantee on flash media (USB sticks, SD/eMMC/
UFS, SSDs): the flash translation layer can silently relocate a logical
overwrite to a different physical page, leaving the original bytes
intact until the controller's own garbage collection reclaims them —
recoverable, in principle, via chip-off forensics.

That addendum's proposed fix was full-disk encryption, on the reasoning
that once the whole volume is ciphertext, an un-erased physical remnant
of a "deleted" file is just ciphertext under the volume key, and the only
secret that actually needs reliable destruction shrinks to that one key.
This document works out what that looks like as an app-level design,
rather than a property SilentDuck has to borrow from (and trust) the
underlying OS's own disk encryption.

## The core idea: crypto-shredding, at sheet granularity

**Never persist pad digits in plaintext.** Every sheet is encrypted at
rest under its own sub-key before it ever touches storage. "Destroying" a
consumed sheet means destroying its (small, ~16–32 byte) sub-key, not
overwriting the sheet's (larger) ciphertext. The ciphertext for a
consumed sheet can sit on disk indefinitely, uselessly — it becomes
unrecoverable the instant its sub-key is gone, regardless of what the
flash controller physically did with the ciphertext bytes underneath it.

This is the standard "crypto-shredding" pattern, scoped to the unit
SilentDuck actually needs to forget independently: one sheet, not the
whole pad. A single master key for the whole file would only get you
"unreadable if stolen," which Addendum 2's threat (chip-off recovery of
a *sheet already meant to be gone*) needs more than.

### Sketch

- A per-pad master key `M` (see "Where keys should live," below).
- Per-sheet sub-keys derived on demand: `sheet_key[n] = KDF(M, n)` for
  sheet index `n` — an HKDF-style derivation, not a reused/rotated `M`
  itself, so that compromising one derived `sheet_key[n]` doesn't expose
  `M` or any other sheet's key.
- Each sheet's digits, encrypted under `sheet_key[n]` (AES-GCM, so
  tampering/corruption is detected rather than silently decrypting to
  garbage), replace the current plaintext `.otk` content on disk — same
  file-per-sheet shape if that's convenient, or rows in an encrypted
  store (see SQLCipher note below); the storage format is a separate
  decision from the scheme itself.
- On consumption: overwrite and drop `sheet_key[n]` (or, if `M` is used
  directly rather than cached per-sheet keys, simply never derive that
  index again and record it as burned). The existing `wipeFile()`-style
  best-effort overwrite is still worth doing on this small key material —
  it just now only has a few dozen bytes to get right instead of an
  entire sheet, and can be backed by a hardware zeroize call where one
  exists (see below) instead of resting entirely on a file overwrite.

## Where keys should live

`M` itself is the one thing that still needs a real destruction/secrecy
guarantee, so where it's stored matters as much as the scheme above:

- **Preferred:** a hardware-backed keystore — Android Keystore/StrongBox,
  iOS Secure Enclave, a TPM via Windows CNG/DPAPI on the desktop client.
  These are specifically engineered to give destroy/zeroize guarantees
  ordinary flash-backed files can't; it's the same primitive iOS/Android's
  own "instant wipe" full-disk encryption rests on. Using it here means
  SilentDuck inherits a real guarantee instead of re-deriving a weaker
  version of the same idea.
- **Fallback**, where no such hardware exists (older/embedded targets,
  the microSD-based fieldcraft use case): `M` as an ordinary small file,
  scrubbed with the existing `wipeFile()` machinery. Weaker, but the
  blast radius of "did the overwrite physically land" has shrunk from a
  whole sheet to one key, which matters even without a hardware backstop.

## What this does and doesn't fix

- **Fixes:** chip-off recovery of a *consumed* sheet's stray physical
  remnants — those remnants are ciphertext with no recoverable key,
  independent of FTL/wear-leveling behavior. This closes the specific gap
  Addendum 2 raised, for the key material specifically (not for
  message-log plaintext or anything else stored unencrypted elsewhere).
- **Doesn't fix:** plaintext exposure while a sheet is *decrypted for
  active use* — that plaintext is in RAM, and if it's ever swapped or
  written into a hibernation image (a live concern for the Windows
  desktop client; less so for phones), the same class of problem
  reappears one layer up. Out of scope for this document; flagged for
  whichever client actually handles decrypted pad material in memory.
- **Doesn't fix, and isn't trying to:** message-log/plaintext-transcript
  storage, or anything covered by the field/HQ retention split already
  discussed elsewhere (field devices meant to be zero-retention by
  design, not to need this scheme to cover a local archive at all).

## Performance

A non-issue. AES hardware acceleration is essentially universal on
targets SilentDuck runs on — ARMv8 Cryptography Extensions on phone
SoCs, AES-NI on x86 desktops, plus the inline crypto engines iOS/Android
already use for their own FDE. Per-sheet-granularity encrypt/decrypt
(252 rows × 25 digits per sheet, per the existing pad structure in
[combinometrics_manual_otp.md](combinometrics_manual_otp.md)) costs
nothing worth measuring against that hardware.

## Relationship to the Zendo/SQLCipher precedent

Zendo's use of encrypted SQLite (almost certainly SQLCipher — the
standard AES-256-encrypted-SQLite-file extension) is a reasonable
building block if SilentDuck's key storage moves to a structured store
rather than flat `.otk` files, but it solves a different, coarser problem
than this document: SQLCipher encrypts the *whole database file* under
one key, which gives "unreadable if the file is stolen or imaged" — a
real win, but not per-sheet independent destroyability. The sub-key
layer above would need to be built on top of SQLCipher (or any other
storage engine), not assumed to come from it for free.

## Open questions / next steps

- Concrete algorithm choices: AES-GCM vs. AES-CTR+separate MAC for
  per-sheet encryption; HKDF (or similar) for `sheet_key[n]` derivation
  from `M`.
- Whether `.otk` files stay file-per-sheet (now encrypted) or move to a
  structured store (SQLCipher or otherwise) — a storage-format decision
  independent of the scheme itself.
- Whether this lands in `otp.py` first (Python, matches the existing
  reference implementation) or in the C++ `Quacque` engine first (which
  `QuacqueGUI` and, eventually, `Yaya` build on) — `Yaya/yaya_design.md`
  already notes the OTP engine gets wired in as a backend object once the
  desktop UI shape is settled; this document is a prerequisite for that
  wiring to carry the same at-rest guarantees the field/HQ and
  scrub-on-exit design already assumes.
- Needs the same treatment the rest of this project gives security
  claims: a demo script exercising real code, not just the argument above
  — nothing here should be treated as more than a proposal until that
  exists.
