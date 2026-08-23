# Manual worked example: combinatorial key expansion by hand

This document works through the `ki`/`kj` combinatorial key-expansion step
(`do_joinKeys()` / `do_unjoinKeys()` in `otp.py`) using pencil-and-paper-style
arithmetic, with every number cross-checked against the actual running code.

It exists for two reasons:

1. **Proof of the human-reproducibility claim.** The whole design premise is
   that an operative without access to a computer can still perform this
   procedure with a pad, a pencil, and the physical key sheets. This walks
   through the real mechanics explicitly enough that a person could extend
   it to the full 252-row procedure by hand — tediously, but mechanically,
   with no step that requires a machine.
2. **A permanent regression reference.** This example happens to exercise
   the code path that had a latent bug: the `checkSumRandomizing`
   checksum-ordering branch, including a genuine checksum collision. If that
   branch ever regresses, re-running the code against the fixed values below
   will catch it — which a same-implementation-against-itself round-trip
   test cannot, since it only proves self-consistency, not correctness
   against an independent, by-hand reference.

**This example key material is illustrative only — never use it for a real
message.** The rows are simple repeating digits specifically so the
arithmetic is easy to follow by eye; real key sheets must always come from
`-g` (true OS randomness). In fact, constant-digit rows are exactly the
degenerate case explored in `combinometrics_analysis.py` Part 1 — don't
reuse this data for anything but following along with this document.

## 1. Example key material

Ten rows, 25 digits each, for both `ki` and `kj`. Each row here is simply
one repeated digit, so a whole row can be tracked as a single number:

| row | `ki` digit | `kj` digit | `z = ki + kj (mod 10)` |
|----:|:----------:|:----------:|:----------------------:|
|   0 |     0      |     7      |            7           |
|   1 |     1      |     8      |            9           |
|   2 |     2      |     9      |            1           |
|   3 |     3      |     0      |            3           |
|   4 |     4      |     1      |            5           |
|   5 |     5      |     2      |            7           |
|   6 |     6      |     3      |            9           |
|   7 |     7      |     4      |            1           |
|   8 |     8      |     5      |            3           |
|   9 |     9      |     6      |            5           |

(The `z` column is the sum each row will *actually* contribute once `ki`
and `kj` are combined — see the security note in §4.)

## 2. Expand two representative combinations

`combo5x10` lists all `C(10,5)=252` five-of-ten row subsets. A real exchange
works through all 252; here we hand-compute just the first two, which are
adjacent in the table and differ by one swapped row — chosen deliberately so
this example also demonstrates the pairwise-difference recovery trick from
`combinometrics_analysis.py`.

- **Combo A** = `01234` → rows {0,1,2,3,4}
- **Combo B** = `01235` → rows {0,1,2,3,5} (row 4 swapped for row 5)

For each combo, `combinateExpandedKeys()` sums the chosen rows digit-by-digit
(mod 10, **no carrying between digit positions** — this is the pitfall to
watch for doing this by hand: it's five separate single-digit additions
repeated 25 times, not one big 25-digit number added five times) and negates
the result:

**`ki`, combo A** (rows 0+1+2+3+4 = 0+1+2+3+4 = 10 at every position):
```
sum   = 10  ->  digit = 0  (10 mod 10)
row   = -(0) mod 10 = 0, repeated 25 times
```
**`ki`, combo B** (rows 0+1+2+3+5 = 0+1+2+3+5 = 11):
```
sum   = 11  ->  digit = 1
row   = -(1) mod 10 = 9, repeated 25 times
```
**`kj`, combo A** (rows 7+8+9+0+1 = 25):
```
sum   = 25  ->  digit = 5
row   = -(5) mod 10 = 5, repeated 25 times
```
**`kj`, combo B** (rows 7+8+9+0+2 = 26):
```
sum   = 26  ->  digit = 6
row   = -(6) mod 10 = 4, repeated 25 times
```

Each expanded row also carries a 5-digit checksum (the row's five 5-digit
groups added together, same no-carry digit-wise rule). Since every row here
is a single repeated digit, each of the five groups is identical, so the
checksum is just that digit added to itself 4 times, digit-wise:

| | expanded row (25 digits) | checksum (5 digits) |
|---|:---:|:---:|
| `ki` combo A | `0` × 25 | `00000` |
| `ki` combo B | `9` × 25 | `55555` |
| `kj` combo A | `5` × 25 | `55555` |
| `kj` combo B | `4` × 25 | `00000` |

Verified against the running code:
```
iTmp[0] (ki, combo 01234): 000000000000000000000000 00000
iTmp[1] (ki, combo 01235): 999999999999999999999999 55555
jTmp[0] (kj, combo 01234): 555555555555555555555555 55555
jTmp[1] (kj, combo 01235): 444444444444444444444444 00000
```

## 3. Combine `ki` and `kj` (digit-wise sum, not concatenation)

`do_joinKeys()`/`do_unjoinKeys()` combine the two expanded tables with
`stringAdd()`, position for position (row and checksum both, since checksum
is itself a linear function of the row — the two combine consistently):

```
tmp[0] = iTmp[0] + jTmp[0] = (0+5)*25, (00000+55555) = 5*25, 55555
tmp[1] = iTmp[1] + jTmp[1] = (9+4)*25, (55555+00000) = 3*25, 55555
```
Verified:
```
combined tmp[0]: 555555555555555555555555 55555
combined tmp[1]: 333333333333333333333333 55555
```

**Both rows produce checksum `55555` — a genuine collision**, arising
naturally from this example (not manufactured). This is exactly the
scenario the checksum-disambiguation loop exists to handle, and exactly the
code path that was silently broken before the list/dict fix.

## 4. Checksum-based ordering, including the collision

Processing `tmp[0]` first (it comes first in the list):
```
checkSumString = "55555"   ->  not in keys yet  ->  keys["55555"] = row of tmp[0] = "5"*25
```
Processing `tmp[1]`:
```
checkSumString = "55555"   ->  ALREADY in keys
  -> bump: csNum = int("55555") + 1 = 55556
  -> checkSumString = "55556"  ->  not in keys  ->  keys["55556"] = row of tmp[1] = "3"*25
```
Verified:
```
keys table: {'55555': '5'*25, '55556': '3'*25}
```

This is the exact mechanism that would have crashed with a `TypeError`
under the pre-fix code (where `tmp` was a dict and this loop's `x` was
silently an integer key rather than the row string it's sliced as).

## 5. Mask with fresh key material and transmit

Pick (for illustration; in real use this is true random data) `rd` values
for the two rows in the order they were keyed above:
```
rd for "55555" slot: 1*25
rd for "55556" slot: 2*25

ct for "55555" slot = rd - row = 1 - 5 = -4 -> +10 = 6, repeated 25 times
ct for "55556" slot = rd - row = 2 - 3 = -1 -> +10 = 9, repeated 25 times
```
Verified: `ct_A = 6*25`, `ct_B = 9*25`. This is what travels over the radio.

## 6. Recipient recovers the new key material

The recipient independently holds the same `ki`/`kj`, so repeating §§1–4
produces the identical `keys` table (deterministically, no coordination
needed beyond both sides having the same two sheets). Recovery is then just
addition:
```
recovered "55555" slot = ct_A + row = 6 + 5 = 11 -> 1, repeated 25 times  =  rd  ✓
recovered "55556" slot = ct_B + row = 9 + 3 = 12 -> 2, repeated 25 times  =  rd  ✓
```
Verified: both recovered values match the sender's original `rd` exactly.

## 7. A note connecting back to the security analysis

`tmp[0]`'s row digit (5) minus `tmp[1]`'s row digit (3) = 2, which is
exactly `z₅ − z₄ = 7 − 5 = 2` from the table in §1 — the same
pairwise-difference relationship `combinometrics_analysis.py` uses to
recover row differences from leaked combination data. Combo A and B differ
by swapping row 4 for row 5, and the combined table exposes only their
difference in `z`-space (`ki+kj`), never `ki` or `kj` individually — the
same information-theoretic gap discussed in the design analysis holds here
too, worked with real numbers instead of abstract argument.

## Summary

A person with a pencil, the physical `ki`/`kj` sheets, and this procedure
can reproduce every step above without a computer — tediously across all
252 combinations for a real exchange, but with no step requiring anything
beyond digit-wise mod-10 addition/subtraction and simple bookkeeping. Every
number in this document was independently verified against the real
`otp.py` functions (`combinateExpandedKeys`, `stringAdd`, `stringSubtract`)
at the time of writing.
