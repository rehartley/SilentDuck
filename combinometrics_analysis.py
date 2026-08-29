#!/usr/bin/python3

# -*- coding: utf-8 -*-

# ########################################### #
# Released under the BSD Zero Clause License  #
# ########################################### #

"""combinometrics_analysis.py

Cryptanalysis / verification harness for the combinatorial key-expansion
scheme in otp.py (`combinateExpandedKeys`) and the two-sheet key-sharing
protocol built on top of it (`do_joinKeys` / `do_unjoinKeys`). Uses the
real 5-of-10 combo table and real 25-digit rows from otp.py throughout --
no shrunk toy version.

Demonstrates, with real numbers:

  PART 1 - Null-space check: shifting every one of the 10 rows of a key
           sheet by the same EVEN digit (mod 10) produces byte-for-byte
           identical combinatorial output. Shifting by an ODD digit does
           not (and lands on a different, but still shared, output).

  PART 2 - Full recovery of every pairwise row difference from a modest
           number of leaked combination rows, via the "two combos that
           differ by exactly one swapped row" subtraction trick.

  PART 3 - The irreducible ambiguity that remains even given every one of
           the 252 leaked rows for a SINGLE sheet: because each of the 25
           digit columns is combined completely independently, we
           brute-force each column against all 252 real equations and
           confirm exactly 5 candidate digits survive per column -- 5**25
           total candidate key sheets, none of which can be told apart
           using this data alone.

  PART 4 - The real, two-sheet `do_joinKeys()` protocol end to end: both
           sheets' 252 expanded rows are pooled, checksum-deduplicated,
           sorted, and split into two 252-row halves that get added
           together and then masked with fresh random data before
           anything is transmitted. Shows (a) that checksum sort mixes
           ki-origin and kj-origin rows into both halves, generalizing
           the small worked example in combinometrics_manual_otp.md;
           (b) that the mixing/split logic here is exercised against the
           REAL otp.do_joinKeys(), through real temp files, not a
           reimplementation, so this can't silently drift from otp.py;
           and (c) that the transmitted ciphertext (`combinedKeyFile`) is
           a genuine one-time mask -- it is equally consistent with any
           key value, so it carries none of Part 1-3's ambiguity itself.
           Parts 1-3's residual ambiguity only matters to someone who
           obtains a sheet's raw combinateExpandedKeys() output directly
           (e.g. before masking); it says nothing about what leaks from
           watching the wire.

  PART 5 - Pins combinometrics_manual_otp.md's fully-worked-by-hand
           example (specific digits, checksums, collisions, and results)
           to the real otp.py functions with hard asserts, so that doc
           can't silently go stale the way it nearly did before (see
           below) -- a future algorithm change fails this script loudly
           instead.

Parts 1-3 test `combinateExpandedKeys()`/`combo5x10` in isolation, which
have not changed since this script was first written. `do_joinKeys()` /
`do_unjoinKeys()` have: the checksum-collision wraparound past '99999' was
fixed (previously could overflow to a 6-digit string and sort wrong), and
a brief experiment combining the two sheets by digit-wise sum instead of
concatenation was tried and then reverted -- the shape of the protocol
Part 4 analyzes (concatenate, checksum-sort, split, add, mask) is the same
one that was there when Parts 1-3 were written.

Run directly:

    python combinometrics_analysis.py
"""

import itertools
import os
import shutil
import tempfile

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
# Part 4: the real two-sheet do_joinKeys() protocol
# ---------------------------------------------------------------------------

def join_checksum_interleave(iTmp, jTmp):
    """Replica of the checksum interleave-and-split step inside
    otp.do_joinKeys()/otp.do_unjoinKeys(): pool ki's and kj's 252
    expanded+checksummed rows into one 504-row list (ki's rows first, in
    combo5x10 order, then kj's), assign each row a slot keyed by its
    trailing 5-digit checksum -- bumping forward mod 100000 on any
    collision, exactly as otp.py does -- sort by checksum, and split the
    sorted 504 rows into two 252-row halves, keysA and keysB.

    Unlike otp.py, this also tracks which sheet each surviving row came
    from (origin), purely so Part 4 can measure how thoroughly the sort
    mixes the two sheets -- otp.py itself has no use for that and
    discards it.
    """
    tmp = [(iTmp[c], 'ki') for c in range(len(iTmp))] + \
          [(jTmp[c], 'kj') for c in range(len(jTmp))]

    keys = {}
    for row_with_checksum, origin_tag in tmp:
        checkSumString = row_with_checksum[-5:]
        row = row_with_checksum[:-5]

        while checkSumString in keys:
            csNum = (int(checkSumString) + 1) % 100000
            checkSumString = '{:05}'.format(csNum)

        keys[checkSumString] = (row, origin_tag)

    sorted_keys = sorted(keys.keys())
    ordered_rows   = [keys[k][0] for k in sorted_keys]
    ordered_origin = [keys[k][1] for k in sorted_keys]

    half = len(ordered_rows) // 2
    keysA, keysB     = ordered_rows[:half],   ordered_rows[half:]
    originA, originB = ordered_origin[:half], ordered_origin[half:]
    return keysA, keysB, originA, originB


def real_do_joinKeys_ct_and_rd(ki, kj):
    """Drive the REAL otp.do_joinKeys() through actual temp files -- not a
    reimplementation -- and return (ct_rows, rd_rows): the transmitted
    ciphertext and the recovered random pad, both as lists of 25-digit
    strings, for as many of the 252 rows as otp.py actually writes to
    disk.

    otp.do_joinKeys() only writes the first 6250 of the 6300 digits it
    generates for the random pad (25 files x 250 digits = 6250; see
    combinometrics_manual_otp.md section 7) -- the last 2 of 252 rows'
    worth of rd never reach a file. So this returns only rows 0..249
    (250 of 252); rows 250-251 are unrecoverable from files by design, on
    both the sending and receiving side alike.
    """
    tmpdir = tempfile.mkdtemp(prefix='combinometrics_')
    try:
        file_i = os.path.join(tmpdir, 'ki.tmp')
        file_j = os.path.join(tmpdir, 'kj.tmp')
        ctFile = os.path.join(tmpdir, 'combined.ct')
        prefix = os.path.join(tmpdir, 'newkey-')

        with open(file_i, 'w', encoding='utf-8') as f:
            f.write(ki)
        with open(file_j, 'w', encoding='utf-8') as f:
            f.write(kj)

        # real otp.py code path: pools iTmp/jTmp, checksum-sorts, splits,
        # adds, masks with fresh randDigits(), writes ct + 25 .otk files,
        # then wipes file_i/file_j (we already have ki/kj saved above).
        otp.do_joinKeys(file_i, file_j, ctFile, prefix)

        ct = otp.loadKeyPad(ctFile)

        rd = ''
        for i in range(25):
            rd += otp.loadKeyPad(prefix + '{:02}'.format(i + 1) + '.otk')

        ct_rows = [ct[c * 25:(c + 1) * 25] for c in range(250)]
        rd_rows = [rd[c * 25:(c + 1) * 25] for c in range(250)]
        return ct_rows, rd_rows
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def part4_two_sheet_join():
    print()
    print('=' * 72)
    print('PART 4: the real two-sheet do_joinKeys() protocol')
    print('=' * 72)

    print('Generating two real, random 10-row key sheets ki, kj (as -g would)...')
    ki = otp.getRandDigits(250)
    kj = otp.getRandDigits(250)

    iTmp = otp.combinateExpandedKeys(ki)
    jTmp = otp.combinateExpandedKeys(kj)

    keysA, keysB, originA, originB = join_checksum_interleave(iTmp, jTmp)
    K_replica = [otp.stringAdd(keysA[x], keysB[x]) for x in range(len(keysA))]

    # (a) how thoroughly does the checksum sort mix the two sheets?
    countA_ki = originA.count('ki')
    countB_ki = originB.count('ki')
    print()
    print('(a) checksum-sort mixing of the two sheets into keysA / keysB:')
    print(f'    keysA: {countA_ki:3d} rows from ki, {len(originA) - countA_ki:3d} rows from kj  (of {len(originA)})')
    print(f'    keysB: {countB_ki:3d} rows from ki, {len(originB) - countB_ki:3d} rows from kj  (of {len(originB)})')
    print('    Neither half is purely one sheet -- generalizes the toy example')
    print('    in combinometrics_manual_otp.md to real, full-size, random sheets.')

    # (b) cross-check the replica against the REAL otp.do_joinKeys(), via
    # real temp files, so this analysis can't silently drift from otp.py.
    print()
    print('(b) cross-checking against the REAL otp.do_joinKeys() (real temp files):')
    ct_rows, rd_rows = real_do_joinKeys_ct_and_rd(ki, kj)
    K_from_real_files = [otp.stringAdd(ct_rows[x], rd_rows[x]) for x in range(len(ct_rows))]
    matches = sum(1 for x in range(len(ct_rows)) if K_from_real_files[x] == K_replica[x])
    print(f'    K recovered from real ct+rd files matches this script\'s replica')
    print(f'    of the checksum-interleave-and-split step on {matches}/{len(ct_rows)} rows')
    print(f'    (rows 0..{len(ct_rows) - 1} of 252 -- the other 2 rows\' rd is real entropy')
    print(f'    that otp.py generates and then never writes to any file; see')
    print(f'    combinometrics_manual_otp.md section 7).')
    print(f'    full match: {matches == len(ct_rows)}')

    # (c) the transmitted ciphertext is a genuine one-time mask: the SAME
    # ciphertext row is equally explainable by two totally unrelated keys.
    print()
    print('(c) the transmitted combinedKeyFile carries none of the above ambiguity itself:')
    ct_real = ct_rows[0]
    rd_true = otp.stringSubtract(K_replica[0], ct_real)
    print(f'    real transmitted ciphertext row 0:      {ct_real}')
    print(f'    true K row 0 (from ki+kj, unknown to an eavesdropper): {K_replica[0]}')
    print(f'    implied real rd row 0 = K - ct:          {rd_true}')
    print(f'    (matches the rd this script read back from otp.py\'s own .otk file: '
          f'{rd_true == rd_rows[0]})')

    K_fake = constant_key(7)[:25]   # a totally unrelated, wrong "K"
    rd_fake = otp.stringSubtract(K_fake, ct_real)
    print(f'    an UNRELATED, wrong K row (e.g. {K_fake}):')
    print(f'    would imply a DIFFERENT rd = K_fake - ct: {rd_fake}')
    print( '    -- also a perfectly ordinary-looking 25-digit string. Nothing about')
    print( '    the transmitted ciphertext row favors the true K over any other;')
    print( '    it is a one-time mask, exactly like the message-encryption step.')
    print( '    (This is otp.py\'s own repudiation property -- see the Schneier')
    print( '    DKHS -> SELL/STOP/BLUE/WFSH example -- applied to the key-sharing')
    print( '    step instead of the message step.) The security of this whole')
    print( '    protocol therefore reduces entirely to keeping ki and kj secret;')
    print( '    Parts 1-3\'s residual ambiguity is about what leaks if a sheet\'s')
    print( '    raw combinateExpandedKeys() output leaks by itself -- not about')
    print( '    what leaks from watching the wire.')


# ---------------------------------------------------------------------------
# Part 5: pin combinometrics_manual_otp.md's worked example to real otp.py
# ---------------------------------------------------------------------------

def part5_verify_manual_doc_worked_example():
    """combinometrics_manual_otp.md walks a tiny, fully-worked-by-hand
    example of the checksum-interleave-and-split step -- specific digits,
    specific checksums, specific collisions, specific results. It's a
    static document with no way to notice if do_joinKeys()'s algorithm
    changes under it (this has happened before: the sheets were briefly
    combined by digit-wise sum instead of concatenation, then reverted --
    see the module docstring above).

    So: reproduce that exact example through the real otp.py functions
    (combinateExpandedKeys, stringAdd, stringSubtract) and this script's
    join_checksum_interleave() (itself cross-checked against the real
    otp.do_joinKeys() in Part 4b), and assert every number the doc prints.
    If do_joinKeys()'s algorithm ever drifts again, this fails loudly --
    a signal that combinometrics_manual_otp.md needs a matching update --
    instead of the doc silently going stale.
    """
    print()
    print('=' * 72)
    print('PART 5: pinning combinometrics_manual_otp.md\'s worked example')
    print('=' * 72)

    # section 1: ki row r = digit r, kj row r = digit (9-r)
    ki = ''.join(str(r)     * 25 for r in range(10))
    kj = ''.join(str(9 - r) * 25 for r in range(10))

    iTmp = otp.combinateExpandedKeys(ki)
    jTmp = otp.combinateExpandedKeys(kj)

    # section 2: the doc only walks combo5x10's first two entries by hand
    assert otp.combo5x10[0] == '01234' and otp.combo5x10[1] == '01235', \
        'combo5x10\'s first two entries changed -- the doc\'s combo choice is stale.'

    assert iTmp[0] == '0' * 25 + '00000', f'ki, combo 01234 expected 0x25+00000, got {iTmp[0]}'
    assert iTmp[1] == '9' * 25 + '55555', f'ki, combo 01235 expected 9x25+55555, got {iTmp[1]}'
    assert jTmp[0] == '5' * 25 + '55555', f'kj, combo 01234 expected 5x25+55555, got {jTmp[0]}'
    assert jTmp[1] == '6' * 25 + '00000', f'kj, combo 01235 expected 6x25+00000, got {jTmp[1]}'
    print('  section 2 (expanded rows + checksums): matches doc  OK')

    # sections 3-4: interleave just these 4 rows (the doc's simplification
    # of the real 504-row pool, for hand-tractability), then combine.
    iTmp2 = {0: iTmp[0], 1: iTmp[1]}
    jTmp2 = {0: jTmp[0], 1: jTmp[1]}
    keysA, keysB, originA, originB = join_checksum_interleave(iTmp2, jTmp2)

    assert keysA == ['0' * 25, '6' * 25], f'expected keysA = [0x25, 6x25], got {keysA}'
    assert keysB == ['9' * 25, '5' * 25], f'expected keysB = [9x25, 5x25], got {keysB}'
    print('  section 3 (checksum collisions, sort, split): matches doc  OK')

    K = [otp.stringAdd(keysA[x], keysB[x]) for x in range(2)]
    assert K[0] == '9' * 25, f'expected K row 0 = 9x25, got {K[0]}'
    assert K[1] == '1' * 25, f'expected K row 1 = 1x25, got {K[1]}'
    print('  section 4 (K = keysA + keysB): matches doc  OK')

    # section 5: the doc's own illustrative rd values (not real randDigits())
    rd = ['3' * 25, '7' * 25]
    ct = [otp.stringSubtract(K[x], rd[x]) for x in range(2)]
    assert ct[0] == '6' * 25, f'expected ct row 0 = 6x25, got {ct[0]}'
    assert ct[1] == '4' * 25, f'expected ct row 1 = 4x25, got {ct[1]}'
    print('  section 5 (ct = K - rd): matches doc  OK')

    # section 6: recipient recovers rd = K - ct
    recovered_rd = [otp.stringSubtract(K[x], ct[x]) for x in range(2)]
    assert recovered_rd == rd, f'expected recovered rd = {rd}, got {recovered_rd}'
    print('  section 6 (rd = K - ct, recipient side): matches doc  OK')

    print()
    print('  every number in combinometrics_manual_otp.md\'s worked example')
    print('  reproduces exactly from the current otp.py -- doc is up to date.')


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

    part4_two_sheet_join()

    part5_verify_manual_doc_worked_example()


if __name__ == '__main__':
    main()
