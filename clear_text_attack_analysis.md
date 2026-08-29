# Clear-text attack analysis: does compromising one message compromise the others?

## Scope

SILENT DUCK's `-j`/`-u` options (`do_joinKeys()` / `do_unjoinKeys()` in
[otp.py](otp.py)) let two parties who already share two old, small key
sheets (`ki`, `kj`) derive and deliver a much larger, fresh 25-sheet
keypad to each other over an open, observed channel. This document asks
one question about that scheme:

> If the plaintext of some of the messages later encrypted with the
> delivered 25-sheet pad is revealed — because it stopped needing to stay
> secret, was declassified, was admitted to, or simply leaked — does that
> let an attacker infer anything about the *other* messages, still
> encrypted under other sheets from the same pad?

Our answer is **no, with high confidence for the delivered pad itself,
and a rigorous "no" for the two seed sheets `ki`/`kj`** — with one part of
the argument resting on empirical testing rather than a completeness
proof, flagged explicitly below rather than glossed over. This document
states the claim precisely, gives the argument, says exactly which part
is proven versus evidenced, and points at the code that backs each piece.

This is not a claim about the whole system's security. It does not cover
key reuse, weak randomness, side channels, traffic analysis, or the
`-e`/`-d` single-message case (which is the ordinary, well-known OTP
fact: one ciphertext plus its true plaintext yields that message's exact
key via `K = P − C`, and nothing about any other, independently generated
key — see `otp.do_fakeMsg()`). Its subject is specifically what a
plaintext leak against the *delivered pad* does or does not expose about
`ki`/`kj`, the *rekeying* channel, and the *other* sheets of the same
delivered pad.

## The construction, briefly

(Full derivation with worked numbers: [combinometrics_manual_otp.md](combinometrics_manual_otp.md).
Reference implementation: [otp.py:1103-1221](otp.py#L1103-L1221).)

- `ki`, `kj`: two pre-shared secret sheets, each 10 rows × 25 digits.
- `combinateExpandedKeys()` expands each sheet into 252 rows — one per
  5-of-10 row subset from the fixed public table `combo5x10` — each row
  being `-(sum of the 5 chosen rows) mod 10`, digit-wise, plus a 5-digit
  checksum folded from that row.
- `do_joinKeys()` pools both sheets' 252 rows into 504, sorts by
  checksum (collisions bumped deterministically), splits at the midpoint
  into `keysA`/`keysB`, and adds them row-wise to get `K` — 252 rows,
  6300 digits, a deterministic function of `ki` and `kj` alone.
- A fresh random pad `R` (`rd` in the code) is drawn from `os.urandom()`
  via `randDigits()`, 25 digits at a time, once per row — **252 separate
  draws**, not one draw reused.
- `ct = K − R` is written to `combinedKeyFile` and sent **in the clear**
  ([otp.py:1206](otp.py#L1206)) — this is "the keys being transmitted."
  All 252 rows of `ct` go out; only the first 250 rows of `R` are written
  to the 25 delivered `.otk` files (rows 250–251 are generated, consumed
  in the mask, and discarded identically on both ends — see
  [combinometrics_manual_otp.md §7](combinometrics_manual_otp.md#L194-L204)).
- `ki`, `kj` are wiped immediately after use and never reused
  ([otp.py:1219-1220](otp.py#L1219-L1220)).

**What an eavesdropper who also reads plaintext of some later messages
actually gets:** `ct` in full (always — it's on the wire), plus, for
every compromised message, the exact `R`-rows that message consumed (via
`R = P − C`, trivial subtraction), and therefore, by addition, the exact
`K`-rows for those same rows (`K = ct + R`). The question is what that
does for the rows they *didn't* get plaintext for.

## The argument

### 1. Ciphertext alone gives nothing

`ct = K − R` with `R` drawn fresh from the OS entropy source is a
one-time mask in the strict OTP sense: for any row, an attacker holding
only `ct` can name a wrong `K` and get an equally well-formed, equally
plausible `R` out the other side. Volume doesn't help — 252 rows of `ct`
with no plaintext are exactly as uninformative as one.
*(Demonstrated: `keysharing_attack_demo.py` Part 1; proven independently
in `combinometrics_analysis.py` Part 4c.)*

### 2. One compromised message → exactly that message's sheet, no more

Each of the 25 delivered sheets occupies a fixed, disjoint block of 10
rows in the 252-row table (sheet *n* = rows `10(n-1)` … `10n-1`). There is
no step in `do_joinKeys()` that expresses one row as a function of
another — every row is an independent slot, populated by an independent
draw from `randDigits()` and an independent entry in the checksum-sorted
504-row pool. Recovering `R` for the rows a compromised message used is
literally the entirety of what that compromise buys; there is nothing
downstream of it to compute.
*(Demonstrated: `keysharing_attack_demo.py` Part 2.)*

### 3. Partial compromise (up to 24 of 25 sheets) — the harder case

This is the case worth being careful about, because it's not simply "no
equation exists" the way case 2 is — it requires ruling out a smarter,
indirect route: using several recovered `K`-rows to back out `ki`/`kj`
themselves, and then recomputing the *un*-compromised rows directly from
the (now known) construction.

That route is real in principle but weaker than it looks, for two
compounding reasons:

- **What a compromised row actually hands you is a sum, not a value.**
  `K[x] = keysA[x] + keysB[x]`, where `keysA[x]` and `keysB[x]` are one
  `ki`-derived combination row and one `kj`-derived combination row,
  paired by rank in a checksum sort over the *pooled* 504 rows. An
  attacker without `ki`/`kj` has no way to recover the checksums (each
  depends on the row content, which requires `ki`/`kj` to compute), so
  they cannot even identify which two of the 504 candidate rows were
  added to produce a given `K[x]` — let alone which sheet each half came
  from. This is a strictly *weaker* observation than direct access to a
  sheet's raw, individual `combinateExpandedKeys()` output.
- **Even the strictly stronger case is already bounded, and the bound is
  substantial.** `combinometrics_analysis.py` Parts 1–3 grant an
  attacker *direct, unsummed* access to all 252 raw rows of a *single*
  sheet — full leakage, no pairing ambiguity at all — and show that even
  then, an exact null space survives: shifting every row of the sheet by
  the same even digit (independently per column) leaves the entire
  252-row output byte-identical, giving exactly 5 surviving candidate
  digits per column and `5**25` (≈ 58 bits) indistinguishable candidate
  sheets. Since what a real partial-compromise attacker gets (unlabeled
  sums, and only for some rows) is strictly less informative than that
  already-bounded best case, it cannot recover `ki`/`kj` any more
  precisely than that bound allows — and plausibly far less precisely,
  given the added unlabeled-pairing obstruction.

We tested this empirically rather than resting on the argument alone:
with 24 of 25 sheets compromised (240 of 252 rows' worth of exact `K`
known), the most direct statistical lever available — using the
compromised rows' digit distribution to guess the 25th sheet's digits —
performs at chance (1-in-10 per digit), not better.
*(Demonstrated: `keysharing_attack_demo.py` Part 3.)*

**What we are and are not claiming here:** the "no shortcut" conclusion
for this case rests on (a) a structural argument bounding the
best-case attacker at `5**25` ambiguity even under a strictly more
generous oracle than reality provides, and (b) an empirical test finding
no signal above chance in the real, weaker case. Neither is a
completeness proof that *no* clever reconstruction of the
unlabeled-assignment problem exists. This exact question — whether
partial known-plaintext lets an attacker unwind `ki`/`kj` "appreciably
faster than ciphertext-only" — is question 3 of
[otp-rekey-post.md](otp-rekey-post.md), posed to outside cryptographers
and still open at time of writing. We flag it as open here rather than
overstate it as closed.

### 4. Full compromise (all 25 sheets) — the worst case, and here we do have a proof

If *every* message ever encrypted with the delivered pad is compromised,
the attacker has all 250 rows of `R`, hence all 250 rows of `K`. Does
that expose `ki`/`kj`? No — and this we can show constructively, not just
argue for. The even-digit-shift null space from case 3 is not merely a
bound on attacker knowledge; it is an exact symmetry of the construction.
For any independent choice of even shift per column (5 choices × 25
columns, applied identically to `ki`'s own 10 rows, and again
independently to `kj`'s), `combinateExpandedKeys()`'s output is
byte-identical, therefore the checksums are identical, therefore the
sort and split are identical, therefore `K` is byte-identical, for **all
252 rows simultaneously** — not just the compromised ones.

We verified this directly against the live code: generated a real
`(ki, kj)`, derived a second, genuinely different `(ki2, kj2)` via
independent per-column even shifts, and confirmed
`compute_K(ki, kj) == compute_K(ki2, kj2)` exactly, across all 252 rows.
Since `ct = K − R` and `R = K − ct` only ever go through `K`, an
attacker handed *everything* the protocol ever produced from one
rekeying event — full `ct` off the wire, full `R` from total plaintext
compromise — still cannot tell `ki` from `ki2`, or `kj` from `kj2`:
`5**25` candidates survive for each, independently (~58 bits apiece,
~116 bits combined). This is moot for that pad's own confidentiality
(the attacker already has the whole thing at that point) but it answers
the narrower, important question directly: **total compromise of a
delivered pad does not hand back the seed keys**, which matters only if
`ki`/`kj` were ever reused or otherwise made to matter again — which
`otp.py` already forecloses by wiping them immediately after use
([otp.py:1219-1220](otp.py#L1219-L1220)).
*(Demonstrated: `keysharing_attack_demo.py` Part 4.)*

## Summary

| Attacker has | Can compute? |
|---|---|
| `ct` only (any number of rows, no plaintext) | Nothing — proven, §1 |
| `ct` + plaintext of 1 message | Exactly that message's own key — trivially, and only that; no leverage on other sheets — proven, §2 |
| `ct` + plaintext of up to 24/25 sheets | No demonstrated leverage on the remaining sheet(s); bounded above by an already-insufficient stronger oracle, and empirically at chance — evidenced, not proven, §3 |
| `ct` + plaintext of all 25 sheets | The entire delivered pad (trivially, sheet by sheet) — but **not** `ki`/`kj`, which retain `5**25` ambiguity each even here — proven, §4 |

The central claim in the title — that a clear-text revelation of some
messages does not compromise the *other* messages from the same key
distribution event — holds structurally and empirically for every
tested case, and holds with a constructive proof for the specific,
important worst case of the seed sheets themselves. The one place we are
not claiming a completeness proof (§3, partial compromise of the
delivered pad) is called out as such, matches an open question already
posed for outside review in `otp-rekey-post.md`, and is the natural next
thing to either strengthen into a proof or falsify with a real attack.

## Addendum: physical disclosure of unused pages, after some pages are already used and destroyed

The body above assumes the attacker learns some rows' key material via a
*plaintext leak* against a message. A related but distinct scenario is
worth its own section because the mechanism of compromise is stronger —
direct, physical exposure rather than an arithmetic recovery — and
because it's the scenario that actually matters for how a delivered pad
gets used operationally over time:

> An adversary intercepts (a) `ct` in transit, still masked, and (b) the
> ciphertexts of messages already sent using some of the delivered pad's
> sheets — with **no known plaintext** for those messages. (c) Those used
> sheets were destroyed after use, per normal operational practice. (d)
> The *remaining, unused* sheets of the same delivered pad are then
> physically disclosed — captured, seized, surrendered — **before** they
> were ever used to encipher anything, so the adversary now holds their
> raw digits directly, in the clear, no arithmetic required.

**Question 1 — how many disclosed unused pages would it take to crack
the used, now-destroyed ones and read the intercepted old messages?**

No demonstrated number does it, from 1 page up to all 24 remaining ones.
Two reasons stack:

- `R`'s rows are **252 independent draws** from `os.urandom()`
  ([otp.py:1192](otp.py#L1192)) — nothing links one row's digits to
  another's by construction. Disclosing sheet 6 says nothing whatsoever
  about sheet 3's digits; this needs no attack analysis, it is simply
  what "independently drawn" means, and it is a stronger, unconditional
  version of the guarantee argued for in the body of this document.
- The only thing that *does* link the rows is the shared `K` mask used to
  transmit them, and `K` does have real structure (it derives from
  `ki`/`kj`). So the only route left is: `ct` (a) + disclosed unused `R`
  (d) → exact `K` for those rows → try to leverage `K`'s structure to
  predict `K`, and hence `R`, for the destroyed rows. That is *exactly*
  §3's partial-compromise question, with the compromise mechanism swapped
  from a plaintext leak to physical seizure — the resulting information
  (some exact `K`-rows, unlabeled with respect to the other rows) is
  identical in kind, so the same `5**25` bound and the same empirical
  chance-rate result apply unchanged.

So the honest framing isn't "N pages suffice" — collecting more disclosed
pages does not demonstrably move an attacker closer to the destroyed
sheets, unlike a threshold scheme where more shares monotonically help.
The only routes to those old messages remain: recovering the physically
destroyed pages themselves (foreclosed if `wipeFile()` actually ran —
[otp.py:1519-1564](otp.py#L1519-L1564)), a plaintext/crib attack on those
specific old messages (outside this document's model), or brute force
(computationally infeasible at these digit counts).

**Question 2 — none of the captured keys help discover the missing
ones?**

Correct, as far as demonstrated — and for the *stronger* of the two
reasons above (unconditional independence of `R`'s own rows), not only
the `K`-structure bound, which only bites if the adversary attempts the
indirect `ct`-mediated route. The same caveat from §3 carries over
unchanged: this rests on a structural bound plus an empirical no-signal
test, not a proof against every conceivable attack on the `K` layer —
but `R`'s row independence itself needs no such hedge.

**Question 3 — does this give a way to share keys via unidirectional
communication?**

Partially, and this is the operationally useful half of the result: it
confirms that broadcasting `ct` in the open, and even later losing the
undelivered remainder of a pad to physical compromise, does not
retroactively burn messages already sent and destroyed under that same
pad — the property a one-way *resupply* channel needs. It is not,
however, unidirectional key **agreement from nothing**: `ki`/`kj` still
have to reach both parties by some other, presumably higher-assurance
channel first (courier, dead drop, prior in-person meeting) before
`-j`/`-u` can stretch them into a fresh pad over the open channel. What
this scheme provides is safe one-way *refresh* of already-shared secret,
not its origination — the same shape classical OTP tradecraft has always
had.

**Question 4 — numbers stations as replenishment plus schedule cover?**

Plausible, and consistent with the general open-source understanding of
what such stations were for — offered here as informed speculation about
real-world tradecraft, not a claim this document can verify, and
separate from the analysis of `otp.py` above. Two distinct, both
independently documented rationales line up with the question:
replenishment (broadcasting fresh key material or pad-keyed messages
openly, safe per Q1–Q3 above), and constant scheduling as traffic-analysis
resistance — transmitting on a fixed cadence regardless of whether a real
message exists that day, padding with noise indistinguishable from real
OTP ciphertext, so that the mere presence or timing of a transmission
carries no signal about operational tempo. The second is a
well-understood category of countermeasure (cover traffic) independent
of OTP specifically, and is widely believed by open-source SIGINT
historians — not confirmed by classified sources — to be part of why
numbers stations kept such rigid schedules.

## Reproducing this

- `keysharing_attack_demo.py` — runs all four scenarios above against
  the real `otp.py`, through real temp files, no reimplementation of the
  cryptographic logic. `python keysharing_attack_demo.py`.
- `combinometrics_analysis.py` — the underlying null-space and
  residual-ambiguity results (Parts 1–3) and the `ct`-is-a-perfect-mask
  result (Part 4c) that §1, §3, and §4 above build on.
- `combinometrics_manual_otp.md` — hand-worked numeric walkthrough of the
  construction itself.
