# Broadcast rekeying: one shared signal, many independent recipients

## Scope

The two prior documents ([clear_text_attack_analysis.md](clear_text_attack_analysis.md),
plus its addendum) analyze the `-j`/`-u` key-sharing scheme
(`do_joinKeys()`/`do_unjoinKeys()` in [otp.py](otp.py)) for a single
pre-shared pair of sheets, `ki`/`kj`, belonging to one agent-and-HQ
relationship. This document asks a natural operational question about
scaling that scheme to a real network:

> If HQ has a hundred agents in the field, each already holding their
> own personal, independently pre-shared pair of sheets, can HQ send
> **one** broadcast — one pile of pure random digits, heard by everyone
> including any adversary listening — and have every agent turn that
> same shared signal into their own personal, mutually unrelated,
> replenished keypad?

The answer is **yes**, and it follows directly from the security
properties already established for the single-pair case, with no new
cryptographic machinery needed — only a re-framing of which value in the
existing construction is public and which is agent-specific. This
document states that re-framing precisely, gives the argument, and names
the caveats that come with running it across a real network rather than
one pair.

This is not a claim about the whole system's security, nor about how any
specific historical numbers station actually operated (that part is
informed speculation, not a claim this document verifies) — it is a
narrow claim about what the existing `otp.py` construction supports when
generalized from one recipient to many.

## The re-framing

Recall the single-pair construction: HQ and one agent share `ki`, `kj`.
`do_joinKeys()` derives `K` from them (`combinateExpandedKeys()`, pooled,
checksum-sorted, split, added — a deterministic function of `ki`/`kj`
alone). HQ draws a fresh random pad `R` and transmits `ct = K − R` in the
clear; the agent recovers `R = K − ct`.

For the broadcast case, keep the same arithmetic and swap which side is
generated fresh-and-transmitted versus derived-and-secret:

- HQ generates **one** pile of pure entropy, `B`, and broadcasts it
  directly — not masked by anyone's `K`, just sent as-is. Everyone within
  radio range hears the identical digits.
- Every agent *n* computes their own `K_n = compute_K(ki_n, kj_n)`,
  locally, from their own personal, independently pre-shared sheets —
  exactly `do_joinKeys()`'s deterministic core, run with nobody else's
  input.
- Each agent derives their own personal pad: `pad_n = K_n − B`.
- HQ, holding a copy of every agent's `(ki_n, kj_n)` (that's what makes
  it HQ), computes the same `K_n` and the same `pad_n` for every agent,
  independently, without sending anything agent-specific at all.

`B` now occupies the role `ct` occupied before — public, broadcast,
observed by anyone — and each agent's `K_n` occupies the role `K`
occupied before — secret, and this time also *individual*, since every
agent's `(ki_n, kj_n)` is their own. The arithmetic (`pad = K − (public
value)`) is identical; only the direction of "who generates what" has
been swapped, and it still checks out: `stringSubtract` doesn't care
which operand was drawn by HQ locally versus received over the air.

## Why the existing security argument carries over unchanged

Nothing about the two established results depends on `R`/`B` being
generated privately versus broadcast openly, or on there being one
recipient versus many:

- **Confidentiality against an outside adversary** rested entirely on
  `K` (or here, `K_n`) being secret and unknown to anyone without the
  matching `(ki, kj)` — never on the public value's own randomness (see
  `clear_text_attack_analysis.md` §1). `B` being "purely random" and
  fully public changes nothing about this argument; an adversary who
  knows `B` but not `K_n` gets a `pad_n` indistinguishable from any other
  possible `pad_n`, for exactly the same reason `ct` alone never favored
  the true `R` over any other.
- **Independence across recipients** falls straight out of independence
  across `(ki, kj)` pairs. Since each agent's sheets were generated
  separately (as `-g` would generate any sheet — independent draws from
  `os.urandom()`), each agent's `K_n` is unrelated to every other agent's
  `K_m`. Recovering one agent's `K` from a full compromise gives zero
  leverage on any other agent's `K`, for the same reason recovering one
  message's key in the single-pair analysis gave zero leverage on
  another sheet's key (`clear_text_attack_analysis.md` §2–3): there is no
  equation connecting them, because there was never a shared secret
  input to begin with.
- **The seed-sheet null space survives unchanged.** `compute_K()` is the
  same function regardless of what it later gets combined with — a fresh
  per-pair `R` in the original scheme, a shared broadcast `B` here. The
  even-per-column-digit-shift symmetry that leaves total compromise of a
  delivered pad unable to recover the true `ki`/`kj`
  (`clear_text_attack_analysis.md` §4) is a property of `compute_K()`
  alone and applies identically to any one compromised agent in the
  broadcast variant.

We verified all three directly against the live code rather than resting
on the analogy alone — see Reproducing this, below.

## What this buys operationally

- **One signal, arbitrarily many recipients.** HQ doesn't need a separate
  rekeying transmission per agent; one broadcast, and every listener
  independently derives their own unrelated pad. This scales to a
  hundred agents in three countries exactly as well as it scales to one.
- **Minimal field footprint per agent.** Each agent only ever needs to
  carry their own 500 digits of `ki`/`kj` — small enough to memorize,
  conceal, or destroy quickly — to turn openly-broadcast noise into a
  full 25-sheet working pad, without ever meeting HQ again.
- **This is a plausible fit for how broadcast numbers stations actually
  behave**: unaddressed, heard by everyone, with the individualization
  happening entirely on the receiving end via material only that
  listener already possesses. A scheme that required per-recipient
  transmissions would look nothing like that; this one does.
- **No single agent's capture cascades**, provided the independence
  condition below holds — compartmentalization the network gets "for
  free" from how the sheets were generated, which the broadcast mechanism
  then lets HQ exploit at scale instead of trading away for
  convenience.

## Caveats

1. **HQ's own records become the highest-value target in the network.**
   Someone has to hold every agent's `ki`/`kj` to compute what they'll
   derive and to talk back to them. That store, if it exists in one
   place, is a single point of failure for the *entire* network's future
   keys in a way no individual agent's capture is — inherent to any
   hub-and-spoke design, not introduced by broadcasting, but sharpened by
   scale: a hundred agents' worth of seed material in one place is a
   hundred times the prize.
2. **Independence of every agent's `(ki, kj)` is load-bearing, not
   incidental.** If any two agents' seed sheets are generated from a
   shared pattern, sequence, or master seed rather than independently,
   the compartmentalization argument fails between exactly those two
   agents. This has to be a generation-time discipline, not something the
   broadcast scheme itself can enforce.
3. **Single use per agent still has to hold.** Each agent's `(ki, kj)`
   must mask exactly one `B`, then be destroyed — the same wipe-after-use
   discipline `otp.py` already applies to the pairwise case
   ([otp.py:1219-1220](otp.py#L1219-L1220)).
4. **Synchronized rekeying is a traffic-analysis signal, even though the
   key material itself stays sound.** An adversary who notices many
   agents' transmission patterns shift shortly after the same broadcast
   learns that a network-wide rekeying event happened, independent of
   whether any key was actually broken — the same category of concern
   raised for numbers-station scheduling in the addendum to
   `clear_text_attack_analysis.md`.
5. **Transcription accuracy is now a per-agent reliability question.** A
   digit mis-copied off the air desyncs only that one agent's derived pad
   from HQ's — it doesn't cascade to anyone else — but is presumably part
   of why real broadcasts read digits slowly, in repeated groups: the
   failure mode of a broadcast scheme is silent desynchronization, not a
   security failure, and it is worth catching early.

## Reproducing this

- `broadcast_rekeying_demo.py` — runs the four checks above against the
  real `otp.py`, reusing `compute_K()` and `shift_sheet()` from
  `keysharing_attack_demo.py` rather than reimplementing them:
  - **Part 1** — three independently-seeded simulated agents derive
    three distinct pads from one identical broadcast `B`.
  - **Part 2** — HQ, recomputing from its own stored copies of each
    agent's `ki`/`kj`, reproduces every agent's derived pad exactly,
    confirming no agent-specific transmission is needed.
  - **Part 3** — one agent fully compromised exposes nothing about
    either other agent's `K`.
  - **Part 4** — even that compromised agent's own `ki`/`kj` remain
    ambiguous (`5**25` candidates), unchanged from the single-pair case.
  `python broadcast_rekeying_demo.py`.
- `keysharing_attack_demo.py` / `clear_text_attack_analysis.md` — the
  single-pair results this document generalizes from.
- `combinometrics_analysis.py` / `combinometrics_manual_otp.md` — the
  underlying `compute_K()` construction and its null-space proof.
