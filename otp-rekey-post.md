# Work factor of a two-pad, sorted-combinatorial key-wrapping scheme for manual OTP re-keying

## Background and goal

I implemented a manual (pencil-and-paper) one-time-pad system of a mid 70s historically known type. A distinguishing feature is a scheme for renewing one-time-pad key material between two parties who already share a small amount of pre-distributed secret data, without meeting again in person, and without requiring a computer to execute (it must be doable by hand, digit-wise, in the field). I've done my own analysis below and would like it checked — I'd specifically like to know if there's an attack I'm missing, or if my security argument has a gap.

There is no hubris here - anyone can make a crypto system they cannot crack, as long as they have little enough experience.  I am also aware that pseudo-OTP based crypto key distribution schemes are the perpetual motion / cold fusion devices of the cryptography world.

The operational problem is that pads are finite: a remote recipient eventually runs out, and there is no secure channel available to replenish with fresh material — only an open, observed channel. The current approach is to pre-share a small secret and use it to deploy a large fresh working pad over that open channel; the goal here is to characterise how much security that actually buys.

**To concede the obvious up front, so no one spends an answer on it:** this is *not* information-theoretically secure and cannot be. Far more new key material is transported than the shared secret it starts from, over an observed channel, so by Shannon the transported pad carries at most the entropy of the shared secret against anyone who records the transmission. "You can't stretch a one-time pad" is understood — that isn't the question being asked.

**The actual question is the computational work factor** required to recover the shared secret (and therefore the transported pad) given the broadcast ciphertext, with and without known plaintext.

## The construction

**Shared secret:** two pre-shared OTP sheets, `P` and `Q`. Each is 10 rows × 25 digits (5 groups of 5). That's **500 shared digits total (~1660 bits)** — the whole entropy budget.

From one sheet with rows `r0 … r9`:

1. Form all **C(10,5) = 252** five-row subset sums. For each 5-subset `S`, take `row_S = Σ_{k∈S} r_k`, digit-wise **mod 10, no carry**. (The tool actually uses subtraction because it's easier to do by hand on paper; the sign doesn't change the structure.) This gives a 252 × 25 table.
2. **Sort the table.** This is the load-bearing step for the question below.

Do this for both sheets, producing sorted tables `A` (from `P`) and `B` (from `Q`), each 252 × 25.

3. **Combine, don't concatenate.** The wrapping keystream is `K = A + B`, added row-wise / digit-wise mod 10. (An earlier draft of the code concatenated the two tables into 504 rows — that version is weaker and is *not* what's being asked about here. Combining means an attacker only ever sees the sum of the two tables, never `A` or `B` alone.)

4. Generate a **fresh random working pad `R`** locally, the same size as `K` (6300 digits).
5. Broadcast `C = R − K` (mod 10) over the open channel.

The receiver holds `P` and `Q`, recomputes `A`, `B`, `K` deterministically, and recovers `R = C + K`. Both ends now share the fresh pad `R`.

An eavesdropper sees `C`, and eventually sees traffic encrypted under `R`.

**Threat model:** Kerckhoffs's assumption throughout. The fixed enumeration order of the 252 five-row subsets, the checksum formula used to sort them, and every other step of the algorithm are public. The only secrets are the 500 digits of `P` and `Q` themselves.

## What this reduces to

Recovering the shared secret from `C` means reconstructing each sheet's 10 rows from (a masked, summed version of) its 252 five-row subset sums. Reconstructing a set from its subset/pairwise sums is the **turnpike / beltway problem** (a.k.a. partial-digest), here in its **cyclic mod-10** form.

The twist of interest: the attacker never sees a clean beltway instance. They see `C = A + B`, the **sum of two independently-seeded, independently-sorted instances**. That superposition is the thing hoped to be doing real work, and it's the part that outside evaluation is needed for.

## What has already been worked out

- **The naive version is trivially broken.** Single sheet, unsorted: each output-row position is a *fixed, known* linear form in the seed, so known plaintext gives a linear system over Z₁₀ and Gaussian elimination recovers everything. Sorting is what kills this — it destroys the map from output-row position back to which subset produced it, converting "solve a linear system" into "reconstruct from an unordered multiset." That's a jump in problem class, not just added seconds. **How big a jump remains the open question.**

- **A known sort-invariant leak.** Each source row appears in C(9,4) = 126 of the 252 subsets, and 126 ≡ 6 (mod 10), so the column-sum of a whole table is `6 × (row-total of the sheet)` mod 10 — and sorting doesn't touch it. So `ΣA`, `ΣB`, and hence `ΣK`, leak sixfold multiples of each sheet's row-totals regardless of the sort. Any attack probably starts here.

- **Column independence.** The 25 digit-columns are independent size-10 instances. So this may be 25 small beltway problems in parallel rather than one big one.

- **A translation symmetry that protects nothing useful.** Shifting a sheet's rows by a constant leaves the subset sums fixed mod 10 (the `5t ≡ 0` coset — five of the ten possible shifts, since arithmetic here is mod 10, not a vector space, so there's no "dimension" to speak of), so that coset is unrecoverable — but the wrapping rows are *invariant* under it, so it's not guarding any key material.

## Questions

1. Given `C = A + B` where each of `A`, `B` is the **sorted** 252-row subset-sum table of an unknown 10-row sheet mod 10, what is the best known attack to recover the sheets (equivalently `K`, equivalently `R`), and its work factor?
2. Does the superposition (seeing only `A + B`, never either table) *materially* harden the single-instance beltway problem — or does it separate cheaply, e.g. via the symmetric-function leaks above?
3. **How much does sorting actually buy?** Is it the difference between a polynomial linear solve and exponential reconstruction, or does the value-multiset still constrain things enough that annealing / CP-SAT recovers the seed quickly in practice?
4. Does the column independence enable a divide-and-conquer that dominates the attack?
5. Under **known plaintext** — cribbing rows of `R` gives the matching rows of `K = R − C` — does partial knowledge of `K` unwind the seeds faster than ciphertext-only?

This scheme does not need to be unbreakable. What's needed is to know whether it's broken *trivially* or only at a work factor that's meaningful for the two shared sheets' worth of entropy. Concrete attacks, reductions to known beltway results, or "here's the annealing landscape and it's smooth/cliff" empirics are all welcome.
