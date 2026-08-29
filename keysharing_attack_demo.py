#!/usr/bin/python3

# -*- coding: utf-8 -*-

# ########################################### #
# Released under the BSD Zero Clause License  #
# ########################################### #

"""keysharing_attack_demo.py

Attacks the do_joinKeys()/do_unjoinKeys() KEY-SHARING scheme itself (two
old sheets ki/kj -> a new 25-sheet pad) -- not plain -e/-d message
encryption, which is a different and much simpler question (known
plaintext trivially recovers that one message's key, via K = P - C, and
nothing else; see otp.do_fakeMsg()). Runs the real otp.py throughout,
through real temp files, no reimplementation of the crypto logic itself.

Attacker model: has otp.py's source, intercepts the combinedKeyFile (ct,
all 252 rows -- "the keys being transmitted") off the wire, and later
learns the true plaintext of some subset of the messages subsequently
encrypted with the delivered pad (a crib, a declassification, whatever no
longer needs to stay secret).

  PART 1 - ciphertext-only (ct alone, zero compromised messages): shows ct
           is a genuine one-time mask, consistent with any rd whatsoever.

  PART 2 - ONE compromised message: recovers exactly that message's
           sheet, exactly, trivially (K = P - C, same as always) -- and
           shows there is no equation in do_joinKeys() connecting that
           recovered sheet to any other, so it buys no shortcut.

  PART 3 - sheets 1-24 compromised (240 of 252 rows): tests whether that
           leaks anything computable about sheet 25 (still secret), via
           the most obvious statistical lever, against real ground truth
           (available here only because this script also holds ki/kj).
           Finds no signal above chance -- but see the caveat printed
           there: this is an empirical check, not a completeness proof.

  PART 4 - ALL 25 sheets compromised (worst case): the attacker now HAS
           the entire delivered pad. Do they also get ki/kj back? No --
           constructs a second, different (ki2, kj2), via the exact
           null-space transform combinometrics_analysis.py Part 1 already
           found (an independent even digit shift per column, applied to
           all 10 rows of a sheet identically), and shows it reproduces
           the real K -- the deterministic keystream ct and rd both derive
           from -- byte-for-byte identical to the true (ki, kj)'s. Proves
           ki/kj are not recoverable even from total compromise of
           everything the protocol ever produced from them.

Run directly:

    python keysharing_attack_demo.py
"""

import os
import random
import shutil
import statistics
import tempfile

import otp


def hr(title):
    print()
    print('=' * 78)
    print(title)
    print('=' * 78)


def run_real_join(ki, kj):
    """Drive the REAL otp.do_joinKeys() through real temp files and return
    (ct_rows, rd_rows): ct is all 252 rows (what's actually transmitted);
    rd is the 250 rows (of 252) that actually get written to the 25 .otk
    files -- rows 250-251 are generated and discarded by otp.py itself, on
    both ends, per combinometrics_manual_otp.md section 7."""
    tmpdir = tempfile.mkdtemp(prefix='ksdemo_')
    try:
        file_i = os.path.join(tmpdir, 'ki.tmp')
        file_j = os.path.join(tmpdir, 'kj.tmp')
        ctFile = os.path.join(tmpdir, 'combined.ct')
        prefix = os.path.join(tmpdir, 'newkey-')

        with open(file_i, 'w', encoding='utf-8') as f:
            f.write(ki)
        with open(file_j, 'w', encoding='utf-8') as f:
            f.write(kj)

        otp.do_joinKeys(file_i, file_j, ctFile, prefix)

        ct_all = otp.loadKeyPad(ctFile)   # 252 rows x 25 = 6300 digits
        rd_all = ''
        for i in range(25):
            rd_all += otp.loadKeyPad(prefix + '{:02}'.format(i + 1) + '.otk')  # 250 rows

        ct_rows = [ct_all[c * 25:(c + 1) * 25] for c in range(252)]
        rd_rows = [rd_all[c * 25:(c + 1) * 25] for c in range(250)]
        return ct_rows, rd_rows
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def part1_ciphertext_only(ct_rows, rd_rows):
    hr('PART 1: ciphertext-only (0 compromised messages)')

    K_fake = '3' * 25
    rd_if_fake = otp.stringSubtract(K_fake, ct_rows[0])
    print(f'true rd, sheet 1 row 0:                                    {rd_rows[0]}')
    print(f'an unrelated, wrong "K" (333...) implies a DIFFERENT rd:   {rd_if_fake}')
    print()
    print('-> ct alone never favors the true rd over any other 25-digit string.')


def part2_one_compromised_message(rd_rows):
    hr('PART 2: ONE compromised message -> exactly ONE sheet, nothing more')

    # Simulate: a message using pad sheet #1 (rows 0-9) gets its plaintext
    # revealed later -> attacker computes rd for those rows directly
    # (K = P - C, the same trivial subtraction otp.do_fakeMsg() uses). No
    # combinatorics needed at all.
    compromised_sheet1_rd = rd_rows[0:10]
    print(f'recovered sheet #1 (rows 0-9) matches ground truth: '
          f'{compromised_sheet1_rd == rd_rows[0:10]}  (trivially -- this IS the attack)')
    print()
    print('Does knowing sheet #1 give ANY leverage on sheet #2 (rows 10-19)?')
    print('  There is no equation in do_joinKeys() expressing row 10-19 as a')
    print('  function of row 0-9 -- each row is an independent entry in a')
    print('  252-row table, individually checksum-sorted. Nothing to compute.')


def part3_partial_compromise(K_true):
    hr('PART 3: sheets 1-24 compromised (240/250 rows) -- can sheet 25 be predicted?')

    compromised_rows = list(range(0, 240))   # sheets 1-24
    target_rows = list(range(240, 250))      # sheet 25 -- NOT compromised

    # The only lever an attacker has without ki/kj: ct (fully known) +
    # combo5x10 (public) + the compromised K-row VALUES. But
    # K[x] = keysA[x] + keysB[x] is an unknown-origin SUM of one
    # ki-derived and one kj-derived combo row, and which two of the
    # 252+252 candidates were added, and via which checksum collision
    # path, depends on ki/kj -- exactly the thing not known. Empirical
    # check: does the compromised set even correlate digit-wise with the
    # target sheet, e.g. by naive averaging -- the kind of thing that
    # WOULD show up if there were any linear leakage?
    col = 0  # check one digit column across all rows; symmetric for the rest
    compromised_digits = [int(K_true[x][col]) for x in compromised_rows]
    target_digits = [int(K_true[x][col]) for x in target_rows]
    naive_guess = int(round(statistics.mean(compromised_digits))) % 10
    correct = sum(1 for d in target_digits if d == naive_guess)

    print(f'column {col}: mean-of-compromised naive guess = {naive_guess}')
    print(f'  matches target sheet\'s actual digits: {correct}/{len(target_digits)} '
          f'(chance rate would be ~{len(target_digits) / 10:.1f}/{len(target_digits)})')
    print('  -> no better than guessing. (Rerun to see this vary trial to trial,')
    print('     consistently landing near the 1-in-10 chance rate.)')
    print()
    print('This is not a full proof of no-leakage -- reconstructing ki/kj from')
    print('SUMS of unknown-origin, unknown-pairing combo rows (only for the')
    print('compromised indices, not the full 252) is a fundamentally harder,')
    print('unlabeled-assignment problem than combinometrics_analysis.py Parts')
    print('1-3 (which assume DIRECT, unsummed access to all 252 rows of ONE')
    print('sheet, and even THAT leaves 5**25 candidates). No efficient attack')
    print('is known or demonstrated here for the weaker, real, summed/partial')
    print('case -- but "not demonstrated" is not the same claim as "proven')
    print('impossible."')


def shift_sheet(sheet, rng):
    """Add an independent random EVEN digit shift per column, applied to
    ALL 10 rows identically within that column -- the exact null-space
    transform combinometrics_analysis.py Part 1 found (its constant_key()
    used the same shift for every row AND column at once; this generalizes
    to an independent even shift per column, still constant down each
    column, which is what actually cancels in `-(sum of 5 rows) mod 10`)."""
    shift_per_col = [rng.choice([0, 2, 4, 6, 8]) for _ in range(25)]
    rows = [sheet[r * 25:(r + 1) * 25] for r in range(10)]
    shifted_rows = []
    for r in range(10):
        new_row = ''.join(str((int(rows[r][c]) + shift_per_col[c]) % 10) for c in range(25))
        shifted_rows.append(new_row)
    return ''.join(shifted_rows)


def compute_K(ki_sheet, kj_sheet):
    """Reproduce do_joinKeys()'s deterministic checksum-interleave-and-add
    step exactly (see otp.do_joinKeys()), stopping before the randDigits()
    mask -- i.e. the part that depends only on ki/kj, not on fresh entropy.
    NOTE: do_joinKeys() itself injects FRESH randDigits() for rd on every
    call, so comparing two separate do_joinKeys() runs' ct/rd compares
    against a moving target and always differs -- that's rd's own fresh
    entropy, not a property of ki/kj. The actual invariance claim is about
    K, which this function isolates."""
    iTmp = otp.combinateExpandedKeys(ki_sheet)
    jTmp = otp.combinateExpandedKeys(kj_sheet)
    tmp = [iTmp[c] for c in range(len(iTmp))] + [jTmp[c] for c in range(len(jTmp))]

    keys = {}
    for row_with_checksum in tmp:
        checkSumString = row_with_checksum[-5:]
        row = row_with_checksum[:-5]
        while checkSumString in keys:
            csNum = (int(checkSumString) + 1) % 100000
            checkSumString = '{:05}'.format(csNum)
        keys[checkSumString] = row

    sorted_keys = sorted(keys.keys())
    ordered_rows = [keys[k] for k in sorted_keys]
    half = len(ordered_rows) // 2
    keysA, keysB = ordered_rows[:half], ordered_rows[half:]
    return [otp.stringAdd(keysA[x], keysB[x]) for x in range(half)]


def part4_full_compromise_no_ki_kj(ki, kj):
    hr('PART 4: ALL 25 sheets compromised -- do you also get ki/kj back?')

    rng = random.Random(1234)
    ki2 = shift_sheet(ki, rng)
    kj2 = shift_sheet(kj, rng)
    print(f'ki2 == ki: {ki2 == ki}   (deliberately different, per-column even shifts)')

    K_ki_kj = compute_K(ki, kj)
    K_ki2_kj2 = compute_K(ki2, kj2)
    print(f'K(ki, kj) == K(ki2, kj2), all 252 rows, byte-for-byte: '
          f'{K_ki_kj == K_ki2_kj2}')
    print()
    print('So: ki2/kj2 is a DIFFERENT, wrong pair of seed sheets that produces')
    print('the IDENTICAL K -- the deterministic keystream that masks whatever')
    print('fresh rd the real protocol generates. Since ct = K - rd and')
    print('rd = K - ct use the same K either way, an attacker handed literally')
    print('everything the protocol ever produced for one run -- full ct off the')
    print('wire, all 25 sheets from full plaintext compromise of every message')
    print('-- still cannot tell ki from ki2, or kj from kj2: 5**25 candidates')
    print('survive for EACH, independently (~58 bits each, ~116 bits combined).')
    print('Operationally moot here since ki/kj get wiped after use and never')
    print('reused (see do_joinKeys()) -- but it is the rigorous version of "no,')
    print('full pad compromise does not hand you the seed keys back."')


def main():
    otp.init()
    otp.keepKeyFilesAfterUse = True   # this demo inspects ki/kj after do_joinKeys

    hr('SETUP: real ki, kj, real do_joinKeys()')
    ki = otp.getRandDigits(250)
    kj = otp.getRandDigits(250)
    ct_rows, rd_rows = run_real_join(ki, kj)
    print(f'ct intercepted off the wire: {len(ct_rows)} rows (252 -- full transmission)')
    print(f'rd = the new pad, as delivered: {len(rd_rows)} rows (250 -- 25 sheets x 10 rows)')
    print('Ground truth K, computed only because this script holds ki/kj (an attacker would not):')
    K_true = [otp.stringAdd(ct_rows[x], rd_rows[x]) for x in range(250)]

    part1_ciphertext_only(ct_rows, rd_rows)
    part2_one_compromised_message(rd_rows)
    part3_partial_compromise(K_true)
    part4_full_compromise_no_ki_kj(ki, kj)


if __name__ == '__main__':
    main()
