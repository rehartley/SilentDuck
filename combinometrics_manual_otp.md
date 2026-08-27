# Manual worked example: combinatorial key expansion by hand

This works through the `ki`/`kj` combinatorial key-expansion step exactly as
implemented in `otp.py` — `combinateExpandedKeys()`, `do_joinKeys()`, and
`do_unjoinKeys()`, using `stringAdd()`/`stringSubtract()` and the fixed
`combo5x10` table — with small illustrative numbers so every step can be
checked by eye. It exists to show that the whole procedure is doable with a
pencil and the two physical key sheets: every operation below is a single
digit-wise mod-10 addition or subtraction, repeated a fixed number of times,
with no step that needs a machine.

**This example's key material is illustrative only — never use it for a real
message.** Each row here is a single digit repeated 25 times, chosen purely
so the arithmetic can be tracked by eye. A real key sheet must come from
`-g`, which fills each of the 250 digits per page independently from
`os.urandom()` — never from a repeating pattern.

## 0. The real sizes, from the source

- `SHEETSIZE = DIGITSPERGROUP(5) * GROUPSPERLINE(5) * LINESPERPAGE(10) = 250`
  digits — one key file (`ki` or `kj`) is a page of 10 rows of 25 digits.
- `combo5x10` lists all `C(10,5) = 252` five-of-ten row subsets, in a fixed,
  public order. `combinateExpandedKeys()` walks every entry.
- Each sheet expands to 252 rows of 30 digits (a 25-digit combination row
  plus a 5-digit checksum).
- `do_joinKeys()` pools both sheets' 252 rows into one 504-row table, sorts
  it by checksum, and splits the sorted result at its midpoint into two
  252-row halves, `keysA` and `keysB`.
- The two halves are added row-wise to make a 252-row, 6300-digit keystream,
  which masks a freshly generated 6300-digit random string. Only the first
  6250 of those 6300 digits are written out, as 25 files of 250 digits each
  — the last 50 digits are generated (real entropy) but never saved, on
  both the sending and receiving side identically (see §7).

This document only computes 2 of the real procedure's 252 rows per sheet,
enough to show the mechanics — including a genuine checksum collision that
falls out of the small example, not manufactured.

## 1. Example key material

`ki` and `kj` are each 10 rows of 25 digits. Every row here is one repeated
digit, so a whole row is tracked as a single number:

| row | `ki` digit | `kj` digit |
|----:|:----------:|:----------:|
|   0 |     0      |     9      |
|   1 |     1      |     8      |
|   2 |     2      |     7      |
|   3 |     3      |     6      |
|   4 |     4      |     5      |
|   5 |     5      |     4      |
|   6 |     6      |     3      |
|   7 |     7      |     2      |
|   8 |     8      |     1      |
|   9 |     9      |     0      |

## 2. Expand two combinations with `combinateExpandedKeys()`

The first two entries of `combo5x10` are `"01234"` (rows {0,1,2,3,4}) and
`"01235"` (rows {0,1,2,3,5} — row 4 swapped for row 5). For each combo,
`combinateExpandedKeys()` starts `s` at 25 zeros and subtracts each chosen
row from it in turn (`stringSubtract`), digit-by-digit, mod 10, **no
carrying between digit positions**:

```
s = 0 - r_a - r_b - r_c - r_d - r_e   (mod 10, at every digit position)
```

**`ki`, combo `01234`** (rows 0+1+2+3+4 = 10):
```
s = -(10 mod 10) = -0 = 0, repeated 25 times
```
**`ki`, combo `01235`** (rows 0+1+2+3+5 = 11):
```
s = -(11 mod 10) = -1 mod 10 = 9, repeated 25 times
```
**`kj`, combo `01234`** (rows 9+8+7+6+5 = 35):
```
s = -(35 mod 10) = -5 mod 10 = 5, repeated 25 times
```
**`kj`, combo `01235`** (rows 9+8+7+6+4 = 34):
```
s = -(34 mod 10) = -4 mod 10 = 6, repeated 25 times
```

Each row then gets a 5-digit checksum: its five 5-digit groups added
together (`stringAdd`, folding left to right, same no-carry rule). Since
every row here is a constant digit `d` repeated 25 times, each of the five
groups is identical, so the checksum works out to `5*d mod 10`, repeated 5
times — which is only ever `00000` (d even) or `55555` (d odd):

| | expanded row (25 digits) | checksum |
|---|:---:|:---:|
| `iTmp[0]` (`ki`, `01234`) | `0` × 25 | `00000` |
| `iTmp[1]` (`ki`, `01235`) | `9` × 25 | `55555` |
| `jTmp[0]` (`kj`, `01234`) | `5` × 25 | `55555` |
| `jTmp[1]` (`kj`, `01235`) | `6` × 25 | `00000` |

(A repeated-digit row is a degenerate case worth flagging: because the
checksum is just `5*d mod 10`, it collapses to one of only two possible
values, entirely determined by the parity of `d`. Real key data, with
independent random digits per position, doesn't have this degeneracy.)

## 3. Pool both sheets and sort by checksum

`do_joinKeys()` concatenates `iTmp`'s 252 rows with `jTmp`'s 252 rows into
one list — here just 4 entries, `ki`'s two rows followed by `kj`'s two:

```
tmp[0] = iTmp[0] = 0×25, checksum 00000
tmp[1] = iTmp[1] = 9×25, checksum 55555
tmp[2] = jTmp[0] = 5×25, checksum 55555
tmp[3] = jTmp[1] = 6×25, checksum 00000
```

It then builds a dictionary keyed by checksum, walking the list in order and
bumping any colliding checksum to the next value (`(int(checksum)+1) % 100000`)
until a free slot is found:

```
tmp[0]: checksum 00000 -> free       -> keys["00000"] = 0×25
tmp[1]: checksum 55555 -> free       -> keys["55555"] = 9×25
tmp[2]: checksum 55555 -> TAKEN      -> bump to 55556 -> keys["55556"] = 5×25
tmp[3]: checksum 00000 -> TAKEN      -> bump to 00001 -> keys["00001"] = 6×25
```

Both collisions in this tiny example are genuine, not staged — they fall
straight out of the parity degeneracy noted above. Sorting the resulting
keys lexicographically (`sorted(keys.keys())`) — equivalent to numeric order
here, since all keys are 5-digit zero-padded — gives:

```
"00000" < "00001" < "55555" < "55556"
```

so the sorted row order is `[0×25, 6×25, 9×25, 5×25]`. Splitting this at the
midpoint gives:

```
keysA = [0×25, 6×25]
keysB = [9×25, 5×25]
```

Notice `keysA` ends up holding one row that started life in `ki` (`0×25`,
checksum `00000`) and one that started in `kj` (`6×25`, checksum bumped to
`00001`) — likewise `keysB` mixes a `ki`-origin row (`9×25`) with a
`kj`-origin row (`5×25`). The checksum sort interleaves rows from both
sheets into each half in a way that depends on the sheets' own data, not on
a fixed, public rule — even though the whole computation is fully
deterministic and exactly reproducible by anyone holding the same `ki`/`kj`.

## 4. Combine into the keystream

`do_joinKeys()` adds the two halves row by row (`stringAdd(keysA[x], keysB[x])`):

```
row 0: keysA[0] + keysB[0] = 0 + 9 = 9  (mod 10), repeated 25 -> K row 0 = 9×25
row 1: keysA[1] + keysB[1] = 6 + 5 = 11 -> 1 (mod 10), repeated 25 -> K row 1 = 1×25
```

`K` (2 rows here; 252 rows / 6300 digits in the real procedure) is the
keystream that will mask the freshly generated random pad material.

## 5. Mask with fresh random data and transmit

For each row, `do_joinKeys()` draws a fresh random row `rd` (`randDigits(25)`
in the real code — this is the new pad material being delivered) and
transmits `K - rd` (`stringSubtract`). Picking illustrative values
`rd row 0 = 3×25`, `rd row 1 = 7×25`:

```
ct row 0 = K row 0 - rd row 0 = 9 - 3 = 6, repeated 25
ct row 1 = K row 1 - rd row 1 = 1 - 7 = -6 -> +10 = 4, repeated 25
```

`ct` (here `6×25` then `4×25`) is written to the `combinedKeyFile` and sent
over the open channel — in the real procedure this is 6300 digits.

## 6. Recipient recovers the new pad

`do_unjoinKeys()` takes the same two sheets `ki`, `kj` and repeats §§1–4
exactly, landing on the identical `keysA`, `keysB`, and `K` — no coordination
beyond both sides already holding the same two key sheets. It then reads
`ct` from the `combinedKeyFile` and recovers `rd` with `stringSubtract(K, ct)`:

```
row 0: K - ct = 9 - 6 = 3, repeated 25  ==  rd row 0 ✓
row 1: K - ct = 1 - 4 = -3 -> +10 = 7, repeated 25  ==  rd row 1 ✓
```

Both sides now hold the identical freshly generated pad material, without
it ever having been sent anywhere in the clear — only `ct = K - rd` was.

## 7. What's discarded in the real (252-row) procedure

Scaling this up: `do_joinKeys()`/`do_unjoinKeys()` process all 252 rows,
producing 6300 digits of freshly generated random material (`newRandomKeyStr`).
But the file-writing loop is `for i in range(25): newRandomKeyStr[i*250:(i+1)*250]`
— 25 files of 250 digits is only 6250 digits. The last 50 digits of the
6300 generated are never written to any file, on either side. Since both
sides compute the identical `newRandomKeyStr` and apply the identical
slicing, this doesn't desynchronize sender and recipient — it's simply 50
digits of real, freshly consumed entropy (about 0.8% of the batch) that is
generated and then thrown away rather than turned into usable key material.

## Summary

Every step above — subtracting up to five 25-digit rows, folding a row into
a 5-digit checksum by addition, sorting short numeric strings, adding two
rows, subtracting a row from another — is arithmetic a person can do with a
pencil, the two physical `ki`/`kj` sheets, and enough patience to repeat it
252 times per sheet instead of twice. Nothing in the procedure requires a
computer; `otp.py` only makes doing it 252 times fast rather than merely
possible.
