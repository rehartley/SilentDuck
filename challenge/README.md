# SilentDuck rekeying challenge — the raid scenario

**The question:** an operator using the `-j`/`-u` rekeying scheme
([SilentDuck_rekeying_as_a_KDF.md](../SilentDuck_rekeying_as_a_KDF.md),
put to outside review in
[otp-rekey-post.md](../otp-rekey-post.md)) is caught. Everything on them at
that moment is seized. Given the best case an attacker could realistically
end up with, **can the contents of their secret message be recovered?**

This is [Schneier's Law](../SchneiersLaw.md) as a testing methodology, not
a request for anyone's blessing: publish the generous case, let anyone try,
and either nobody breaks it (meaningful evidence) or somebody does
(something worth knowing before it ever protects a real secret).

## The scenario

[`doit.cmd`](doit.cmd) plays out, start to finish, exactly what a field
raid would hand an attacker, using the unmodified reference tool
(`otp.cmd` → `otp.py`):

1. Generate a starting keypad, keep only the last two sheets actually used
   (`key1.otk`/`key2.otk`, the pre-shared secret `k_i`/`k_j`) — 500 digits,
   never transmitted, the entire security budget.
2. Run `-j` on them to broadcast a rekey: `combinedkey.otk` is the
   ciphertext `C` that goes out over the open channel — anyone could have
   intercepted this regardless of whether a raid ever happens. This also
   produces the sender's own local copy of the new 25-page pad
   (`keys/BB-*.otk`).
3. Run `-u` on the same two sheets against `combinedkey.otk` to
   independently re-derive that same pad the way the legitimate receiver
   would (`keys/CC-*.otk`) — `doit.cmd` diffs `BB-001.otk` against
   `CC-001.otk` as a build-time sanity check that both sides land on
   byte-identical key material.
4. Encipher one real message with page 1 of the pad — typed directly into
   the on-screen `EDITOR`, never written to disk as plaintext — then
   decipher it back through the receiver's independently-derived copy
   (`CC-001.otk`) as a second sanity check, also shown only in `EDITOR`,
   also never touching disk.
5. Everything generated (`k_i`, `k_j`, every page of the pad, the
   ciphertext) is archived into a private answer-key bundle *before*
   anything is deleted, and its `SHA-256` is committed publicly — see
   [Verification](#verification). Only then is the actually-used page
   (`CC-001.otk`) deleted, simulating the OPSEC the tool enforces
   automatically in the field, and the public artifacts are packaged
   separately from what's left.

The operator is caught *after* all of that. What an attacker ends up with
is exactly what's public in this folder:

- [`combinedkey.otk`](combinedkey.otk) — the full rekey ciphertext `C`
  (252 rows × 25 digits).
- [`secret_msg.otp`](secret_msg.otp) — the intercepted message, Morse-cut
  shorthand, enciphered under the one page that's *not* here.
- [`challenge_artifacts.tgz`](challenge_artifacts.tgz) (integrity hash in
  [`challenge_artifacts.tgz.hash.txt`](challenge_artifacts.tgz.hash.txt)) —
  the above two files plus `keys/CC-002.otk` … `CC-025.otk`, the 24 pages
  of the new pad that hadn't been used yet. Real key digits, not
  ciphertext.

Not present, and not recoverable from anything public here:

- **`k_i`, `k_j`** (`key1.otk`/`key2.otk`) — never published.
- **`CC-001.otk`** — the page the message was actually enciphered under.
  Destroyed the moment it was used, per step 5 above — exactly the OPSEC
  the tool is supposed to enforce.
- **The plaintext** — typed straight into `EDITOR` and never touched disk,
  on either the enciphering or the deciphering side (see the "Reading/
  writing text without touching the file system" note in `otp -hh`).
  Nobody involved has a saved copy of it — this is the honest "best case
  for the defender, worst case for cryptanalysis" version of the scenario,
  not a contrived one.

`combinateExpandedKeys()`, `do_joinKeys()`, `do_unjoinKeys()` in
[`otp.py`](../otp.py) describe exactly what produced these files. Anyone
can reproduce a fresh instance of this same scenario by running
[`doit.cmd`](doit.cmd) again — it generates a brand-new random `k_i`/`k_j`
each time.

## What actually has to be broken

Recovering the message plaintext factors into two very different steps:

1. **Recover `CC-001.otk`.** `-j` masks with a straight digit-wise mod-10
   subtract, no carrying: `C = K - R`, row by row, where `K` is the
   252-row table built from `k_i`/`k_j` and `R` is the fresh pad. `C` is
   known in full, and `R` is known in full for pages 2–25 — so
   `K_row = C_row + R_row` for 240 of `K`'s 252 rows, for free. `CC-001`
   corresponds to `K`'s first 10 rows; the remaining 2 rows (of 252) are
   never written to *any* page on either end, ever (see the `[:6250]`
   slice in `do_joinKeys()`) — so the honest target is **recovering 12
   missing rows of `K` from 240 known rows**, the public `combo5x10`
   table, and the checksum-sort/pairing rule that decided which two
   combination-rows of `k_i`/`k_j` summed into each row of `K`. This is the
   row-difference / rank-sorted-subset-sum question asked in
   [otp-rekey-post.md](../otp-rekey-post.md#questions) — this scenario is a
   live instance of it, not a restatement.
2. **Decode the message.** Once `CC-001` is known, decrypting
   `secret_msg.otp` is *not* a cryptanalysis problem — it's the same
   mechanical `-d` step any legitimate recipient runs: undo the Morse-cut
   shorthand, add the pad back digit-wise, run the straddling-checkerboard
   decode. All of the security lives in step 1; step 2 is free the moment
   step 1 succeeds.

So the real question restates as: **is 240 known rows of `K` enough to pin
down the last 12**, against the public construction and unlimited offline
compute? If yes, the message falls out for free. If the row-recovery
problem holds, the message is safe regardless of how much else was seized.

## Verification

Before anything was published, the complete answer key — `k_i`, `k_j`,
every page of the derived pad, `combinedkey.otk`, `secret_msg.otp` — was
archived and hashed:

```
SHA-256 (challenge_solution.tgz): <see challenge_solution.tgz.hash.txt>
```

The archive itself is **not** published — only its hash, committed now so
a later reveal can be checked against it instead of taken on trust. That
hash is a commitment to one specific `tar`/`gzip` byte stream, though, not
to the underlying secret digits — file order, timestamps, and compression
settings all affect it, so it isn't something a solver's independently
recovered digits could ever be checked against directly, even if they're
exactly right. What a solver *can* check a recovered answer against is
this, computed straight from the same private archive over the bare digit
strings (no formatting, no line breaks):

```
k_i (key1.otk)               sha256: c7bb0084151ccda65235e087219febfee7a27e0b94e082523b329f2ad4542341
k_j (key2.otk)                sha256: 6b62fb965cbb8c6f70947079ed360dd6fe3ac6901b11ed64affda51018843ca0
k_i || k_j (concatenated)     sha256: 5d2745a7795b385efb851142da0bcd024c0146467fe8862833be2dfcf3d777d2
withheld page (CC-001.otk)    sha256: 46dcbca698f8b125d8a9b142e5ee1b82b6d646f62052f0587dc4393e1396fd2b
```

Reduce a recovered page or sheet to its bare 250 digits, hash it, and
compare. A match on the withheld page alone is a **partial break** — it
shows the missing-row-recovery attack works. A match on `k_i`/`k_j` is a
**full break** — it means every page of this pad, not just the one used
here, was always readable.

There's still no way to check the plaintext itself this way — it has no
saved copy anywhere, including in the private archive above, since it
never touched disk on either end (see above). A correct `CC-001` recovery
makes decrypting `secret_msg.otp` a free mechanical step, though, and
correct straddling-checkerboard plaintext is self-evidently readable text,
not noise — that's the practical check.

If you conclude this is intractable, an explanation of *why* — a reduction
to a known hard problem, a work-factor bound, a clear argument for why a
given avenue doesn't help — is exactly the outcome that makes a negative
result worth something. Partial progress (some but not all of the 12
missing rows, or a demonstrated reduction in the residual search space) is
also worth reporting short of a full recovery.
