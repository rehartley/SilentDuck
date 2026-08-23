#!/usr/bin/python3

# -*- coding: utf-8 -*-

# ########################################### #
# Released under the BSD Zero Clause License  #
# ########################################### #

"""combinometrics_analysis.py

Cryptanalysis / verification harness for the combinatorial key-expansion
scheme in otp.py (`combinateExpandedKeys`, used by `do_joinKeys` /
`do_unjoinKeys`). Uses the real 5-of-10 combo table and real 25-digit rows
from otp.py throughout -- no shrunk toy version.

Demonstrates, with real numbers:

  PART 1 - Null-space check: shifting every one of the 10 rows of a key
           sheet by the same EVEN digit (mod 10) produces byte-for-byte
           identical combinatorial output. Shifting by an ODD digit does
           not (and lands on a different, but still shared, output).

  PART 2 - Full recovery of every pairwise row difference from a modest
           number of leaked combination rows, via the "two combos that
           differ by exactly one swapped row" subtraction trick.

  PART 3 - The irreducible ambiguity that remains even given every one of
           the 252 leaked rows for a sheet: because each of the 25 digit
           columns is combined completely independently, we brute-force
           each column against all 252 real equations and confirm exactly
           5 candidate digits survive per column -- 5**25 total candidate
           key sheets, none of which can be told apart using this data
           alone.

Run directly:

    python combinometrics_analysis.py
"""

import itertools

import otp


# ---------------------------------------------------------------------------
# Part 1: null-space check on the real scheme
# ---------------------------------------------------------------------------

def constant_key(digit):
    """A synthetic 10-row, 25-digit-per-row key sheet where every digit,
    in every row, is the same value."""
    return (str(digit) * 25) * 10


def part1_null_space_check():
    print('=' * 72)
    print('PART 1: null-space check (which constant row-shifts vanish?)')
    print('=' * 72)

    zero_out = otp.combinateExpandedKeys(constant_key(0))

    print('Shifting every row by the same EVEN digit:')
    for d in (0, 2, 4, 6, 8):
        out = otp.combinateExpandedKeys(constant_key(d))
        identical = all(out[k] == zero_out[k] for k in out)
        print(f'  constant digit {d}: output == shift-by-0 output?  {identical}')

    print('Shifting every row by the same ODD digit:')
    one_out = otp.combinateExpandedKeys(constant_key(1))
    for d in (1, 3, 5, 7, 9):
        out = otp.combinateExpandedKeys(constant_key(d))
        identical_to_zero = all(out[k] == zero_out[k] for k in out)
        identical_to_one  = all(out[k] == one_out[k]  for k in out)
        print(f'  constant digit {d}: output == shift-by-0 output? {identical_to_zero}'
              f'   output == shift-by-1 output? {identical_to_one}')

    print()
    print('Interpretation: adding the SAME even digit to all 10 rows of a')
    print('sheet, at every position, leaves every one of the 252 combination')
    print('rows completely unchanged. That constant-even-shift is the exact')
    print('null space of this transform -- the blind spot no amount of')
    print('leaked combination data can ever see into.')


# ---------------------------------------------------------------------------
# Shared helpers: read a real combinateExpandedKeys() output as
# {frozenset(row indices used): tuple(25 ints)} discarding the checksum.
# ---------------------------------------------------------------------------

def combo_indexset(combo_str):
    return frozenset(int(ch) for ch in combo_str)


def as_int_rows(expanded):
    """expanded: dict as returned by otp.combinateExpandedKeys().
    Returns {frozenset(indices): tuple(25 ints)} keyed by which of the 10
    rows were summed, with the trailing 5-digit checksum stripped."""
    out = {}
    for idx, row_with_checksum in expanded.items():
        combo = combo_indexset(otp.combo5x10[idx])
        row   = tuple(int(ch) for ch in row_with_checksum[:25])
        out[combo] = row
    return out


# ---------------------------------------------------------------------------
# Part 2: recover every pairwise row difference from leaked rows
# ---------------------------------------------------------------------------

def part2_recover_differences(by_set, n_rows=10, col_count=25):
    print()
    print('=' * 72)
    print('PART 2: recovering every pairwise row difference from leaked rows')
    print('=' * 72)

    combos = list(by_set.keys())

    # adjacency[i] = list of (j, diff) where diff[c] = row_j[c] - row_i[c] (mod 10)
    # found from two known combos that differ by exactly one swapped index.
    adjacency = {i: [] for i in range(n_rows)}
    edges_used = 0
    for a, b in itertools.combinations(combos, 2):
        sym = a ^ b
        if len(sym) != 2:
            continue
        i = next(iter(a - b))   # row only in a
        j = next(iter(b - a))   # row only in b
        row_a, row_b = by_set[a], by_set[b]
        diff = tuple((row_a[c] - row_b[c]) % 10 for c in range(col_count))
        adjacency[i].append((j, diff))
        adjacency[j].append((i, tuple((-d) % 10 for d in diff)))
        edges_used += 1

    # BFS from row 0, using only as many edges as needed to connect all rows
    delta = {0: tuple(0 for _ in range(col_count))}
    frontier = [0]
    while frontier:
        node = frontier.pop()
        for j, diff in adjacency[node]:
            if j not in delta:
                delta[j] = tuple((delta[node][c] + diff[c]) % 10 for c in range(col_count))
                frontier.append(j)

    print(f'  rows linked via pairwise differences: {sorted(delta.keys())}')
    print(f'  (out of {n_rows} total rows -- fully connected: {len(delta) == n_rows})')
    return delta


# ---------------------------------------------------------------------------
# Part 3: per-column brute force -- how much ambiguity survives?
# ---------------------------------------------------------------------------

def part3_residual_ambiguity(by_set, delta, true_rows, n_rows=10, col_count=25):
    print()
    print('=' * 72)
    print('PART 3: residual ambiguity -- brute-forcing every column against')
    print('        all 252 real combination equations')
    print('=' * 72)

    combos = list(by_set.keys())
    valid_counts = []
    example_cols_printed = 0

    for col in range(col_count):
        valid = []
        for r0_guess in range(10):
            rows_col = [(r0_guess + delta[i][col]) % 10 for i in range(n_rows)]
            ok = True
            for combo in combos:
                predicted = (-sum(rows_col[i] for i in combo)) % 10
                observed  = by_set[combo][col]
                if predicted != observed:
                    ok = False
                    break
            if ok:
                valid.append(r0_guess)
        valid_counts.append(len(valid))

        if example_cols_printed < 3:
            print(f'  column {col:2d}: candidate values for row-0 digit = {valid}'
                  f'   (true value: {true_rows[0][col]})')
            example_cols_printed += 1

    total_candidates = 1
    for c in valid_counts:
        total_candidates *= c

    print(f'  ... (remaining {col_count - 3} columns omitted for brevity)')
    print()
    print(f'  candidates per column: {valid_counts}')
    print(f'  all columns have exactly 5 surviving candidates: '
          f'{all(c == 5 for c in valid_counts)}')
    print(f'  total candidate key sheets consistent with FULL leakage of all')
    print(f'  252 combination rows: {total_candidates:,}  (== 5**{col_count} = {5**col_count:,})')
    print(f'  that is approximately {total_candidates.bit_length()} bits of')
    print(f'  irreducible ambiguity -- unresolvable by ANY amount of additional')
    print(f'  combination-row leakage, since it is the null space from Part 1.')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    part1_null_space_check()

    print()
    print('Generating a real, random 10-row key sheet (as -g would) to attack...')
    ki = otp.getRandDigits(250)
    true_rows = [tuple(int(d) for d in ki[r * 25:(r + 1) * 25]) for r in range(10)]

    leaked = otp.combinateExpandedKeys(ki)   # simulate: attacker has ALL 252 rows
    by_set = as_int_rows(leaked)

    delta = part2_recover_differences(by_set)

    # sanity: confirm recovered deltas match ground truth exactly
    ok = all(
        delta[i][c] == (true_rows[i][c] - true_rows[0][c]) % 10
        for i in range(10) for c in range(25)
    )
    print(f'  recovered deltas match ground truth exactly: {ok}')

    part3_residual_ambiguity(by_set, delta, true_rows)


if __name__ == '__main__':
    main()
