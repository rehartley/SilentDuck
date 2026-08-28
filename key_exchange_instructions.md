# KEY EXCHANGE INSTRUCTIONS
### How to turn two old code sheets into one brand-new code pad, using nothing but pencil and paper

---

## Before you start

You already have two code sheets, called **Sheet I** and **Sheet J**. Both were made by
writing down random digits, and a copy of each was given to the other party ahead of
time — before this job started. That part is already done. This paper does **not**
cover how those two sheets were made or delivered. It covers only what to do with
them now: how to turn those two old sheets into a big new pad of key material, and
how to get that new pad safely into the other party's hands.

Each sheet has **10 lines**, numbered **Line 0 through Line 9** (top line is Line 0,
not Line 1 — this matters, follow it exactly). Each line has **25 digits**, written as
5 groups of 5 digits, like this:

```
Line 0:  39471 85062 71834 09516 27384
```

A whole sheet, 10 lines of 25 digits, is 250 digits.

When this job is finished, both old sheets (Sheet I and Sheet J) are **worn out and
must be destroyed**. They can never be reused. What you get in exchange is a much
bigger new pad — enough key material for many future messages.

---

## What you need

1. Sheet I and Sheet J (already in your hands).
2. Plenty of scratch paper, ruled in columns if you have it (squared "quadrille"
   paper is best, so digits stay lined up).
3. Two or three pencils and an eraser.
4. A fresh source of random digits for the new pad — the same method your unit
   already uses to make code sheets (dice, drawn numbered tiles, or a pre-made
   strip of random digits). You will need a lot of them. This paper does not
   explain how to make random digits; it assumes you already have a supply.
5. The Master Combination List in Appendix A of this paper. It is not secret —
   it is the same fixed list everyone uses, every time, and it never changes.
   You may keep it, copy it, or memorize it freely.
6. A way to get a message to the other party (radio, courier, mail-drop). It does
   **not** need to be secret or protected. Anyone may read the numbers you send
   under this procedure without being able to use them — that is the whole point
   of the method, and is explained in Part 8 below.

---

## Words you need to know

- **Digit** — a single number, 0 through 9.
- **Group** — five digits written together, like `39471`.
- **Line (or Row)** — five groups together, 25 digits. Sheets I and J each have
  10 lines, numbered 0–9.
- **Table** — a list of lines you have built by working through this paper.
- **Combination** — one entry from the Master Combination List (Appendix A). Each
  combination names 5 of the 10 line numbers on a sheet, such as `01234`, meaning
  "Line 0, Line 1, Line 2, Line 3, and Line 4."
- **Checksum** — a 5-digit number you calculate from a line, used only for putting
  lines into order. It is not secret and it is not part of the final key.
- **Pad** — the new key material you end up with at the end of this job. This is
  what actually gets used later to encipher and decipher messages. It must be
  kept completely secret.
- **Cover table** — the table of numbers you send to the other party over the
  open channel. It is *not* the pad. It is a disguise the pad was hidden inside
  of, and by itself it is useless to anyone who does not also hold Sheet I and
  Sheet J.

---

## The one rule behind all the arithmetic

Every calculation in this job is done **one digit at a time**, straight down the
column. You never carry a ten into the next column, and you never borrow a ten
from the next column. Each of the 25 columns is worked completely on its own.

**Adding two digits:** add them normally. If the answer is 10 or more, drop the
ten and keep only the last digit (that is, subtract 10 from your answer).

**Subtracting one digit from another:** subtract normally. If the answer would
be less than zero, add 10 to it.

That is the entire rule. Here is what it looks like added and subtracted, digit
by digit, top to bottom:

```
    4 7 2 8 5
  + 6 8 3 5 1
  -----------
    0 5 5 3 6      (4+6=10, drop ten, write 0)  (7+8=15, write 5)  (2+3=5)  (8+5=13, write 3)  (5+1=6)

    4 7 2 8 5
  - 6 8 3 5 1
  -----------
    8 9 9 3 4      (4-6=-2, add ten, write 8)  (7-8=-1, write 9)  (2-3=-1, write 9)  (8-5=3)  (5-1=4)
```

Practice this a few times on scrap paper with your own numbers before going any
further. Every step below is nothing but this, done over and over.

---

## The big picture

You are going to do eight things, in order:

1. Expand Sheet I into 252 new lines, using the Master Combination List.
2. Expand Sheet J the same way, into another 252 new lines.
3. Give every one of those 504 lines a 5-digit checksum.
4. Pool all 504 lines into one list and sort it by checksum.
5. Split the sorted list exactly in half, and add the two halves together,
   line by line, to make a new table of 252 lines. This table is your **cover
   key**.
6. Draw 252 fresh lines of random digits — this is your **new pad**.
7. Subtract the new pad from the cover key, line by line. What is left over is
   your **cover table** — safe to send in the open.
8. Send the cover table. Keep the new pad. Destroy Sheet I and Sheet J.

The other party, holding the same Sheet I and Sheet J, does steps 1–5 themselves
(they get exactly the same cover key you did, without you telling them anything),
then uses your cover table to pull the same new pad back out. This is covered in
its own section near the end.

---

## Part 1 — Expand Sheet I

Take the Master Combination List (Appendix A). It has 252 entries, always used in
the same order, numbered 1 through 252. Work through it from the top.

For **each** entry:

1. Write down a blank line of 25 zeros: `00000 00000 00000 00000 00000`.
2. The entry names 5 line numbers from Sheet I (for example, entry 1 is `01234` —
   Lines 0, 1, 2, 3, and 4). Subtract those 5 lines from your line of zeros, one
   at a time, using the no-carry rule above. Order does not matter, but do all 5.
3. What is left after all 5 subtractions is this entry's **expanded line**. Set
   it aside and go to the next entry.

Do this all 252 times. Number your results 1 through 252, matching the entry
numbers in Appendix A. This is slow, careful work — expect it to take real time.
Do not skip entries or do them out of order; the other party is going to do the
exact same 252 entries in the exact same order, and both sides must land on
identical results without comparing notes.

## Part 2 — Expand Sheet J

Do exactly the same thing again, entry by entry through Appendix A, but this
time subtracting lines from Sheet J instead of Sheet I. You will end up with a
second set of 252 expanded lines.

## Part 3 — Give every expanded line a checksum

Now go back through **all 504** expanded lines you just built (252 from Sheet I,
252 from Sheet J) and give each one a checksum, as follows:

1. Split the line's 25 digits into its 5 groups of 5.
2. Add the first two groups together (no-carry rule).
3. Add the third group to that result.
4. Add the fourth group to that result.
5. Add the fifth group to that result.

What you have left is a single 5-digit number — the line's checksum. Write it at
the end of the line. The checksum is not secret and is not part of the final
pad; it only exists to help you sort the lines in the next step.

## Part 4 — Pool and sort by checksum

Now treat all 504 lines (with their checksums) as one single list: the 252 from
Sheet I in order, followed by the 252 from Sheet J in order.

You need to put this list of 504 lines into order by checksum, smallest number
first. Two lines will occasionally come up with the very same checksum. When
that happens, you cannot have two lines "in the same slot," so use this fixed
rule, in the order you originally listed the lines (Sheet I's 252 first, then
Sheet J's 252):

> If a line's checksum is already taken by an earlier line, add 1 to the
> checksum and try again. Keep adding 1 until you find a checksum that is
> still free. If you reach `99999`, the next number wraps back around to
> `00000`.

This changes only which slot a line sits in for sorting purposes — it never
changes the line's 25 digits. Once every line has a final (possibly bumped)
checksum with no two alike, sort all 504 lines by that checksum, smallest to
largest. See the worked example after Part 8 if this bumping rule isn't clear
yet — it is easier to see in numbers than in words.

## Part 5 — Split in half and combine

Your sorted list has exactly 504 lines. Split it down the middle: the first 252
lines (smallest checksums) are **Table A**; the last 252 lines (largest
checksums) are **Table B**.

Now add Table A to Table B, line by line: the 1st line of Table A with the 1st
line of Table B, the 2nd with the 2nd, and so on, all 252 pairs, using the
no-carry addition rule. Each pair makes one new line of 25 digits.

The 252 lines you now have are your **cover key**. Checksums are no longer
needed — throw away the checksum digits, you only used them for sorting.

## Part 6 — Draw the new pad

Now, separately from everything above, draw **252 brand-new lines of 25 random
digits each** — 6,300 fresh random digits in total — using your unit's usual
method for making random digits. This is real, fresh randomness; it has nothing
to do with Sheet I or Sheet J.

This is your **new pad**. From this point forward, guard it exactly as
carefully as you would have guarded Sheet I or Sheet J — it is what will
actually be used to send and read future secret messages. Nobody else, other
than the party you are exchanging keys with, may ever see these digits.

## Part 7 — Cover the new pad

Take your cover key from Part 5 (252 lines) and your new pad from Part 6 (252
lines). Subtract the new pad from the cover key, line by line, using the
no-carry subtraction rule (1st cover-key line minus 1st new-pad line, and so
on, all 252 pairs).

The result — 252 lines, 6,300 digits — is your **cover table**. This is what
you are allowed to send in the open. On its own, without Sheet I and Sheet J,
it reveals nothing.

## Part 8 — Send it, save it, destroy the rest

1. Write out the cover table (252 lines, grouped 5 digits per group, 5 groups
   per line, same as your original sheets) and send it to the other party by
   whatever means you have. It does not need to be protected — treat it the
   same as any other piece of paper. Anyone may see it.
2. Take your new pad (252 lines from Part 6) and copy it out onto fresh sheets
   in the standard format — 10 lines of 25 digits per sheet. 252 lines makes 25
   full sheets with 2 lines left over; **stop after the 25th full sheet and
   throw the last 2 lines away, unused.** The other party will do the same and
   land on the same stopping point, without you telling them to — it is a fixed
   rule of the method, not something you coordinate.
3. Once the cover table is sent and the new pad sheets are copied out and safely
   stored, **destroy Sheet I and Sheet J completely** — burn them, or whatever
   your unit's rule is for used key material. They are spent and must never be
   used again, by either party.

---

## What the other party does

The other party already holds the same Sheet I and Sheet J you started with.
When they receive your cover table, they do the following:

1. They repeat Parts 1 through 5 above, themselves, from scratch — expand both
   sheets, checksum all 504 lines, pool and sort them (using the exact same
   bump-on-collision rule), split in half, and add the halves together. Because
   they are working from the identical Sheet I and Sheet J, working the
   identical Master Combination List, in the identical order, they arrive at
   the exact same 252-line cover key you did — without you sending them
   anything about it.
2. They take their own cover key and **subtract** the cover table you sent from
   it, line by line — the same no-carry subtraction as Part 7, just done with
   the cover key and cover table swapped into the roles Part 7 used for the
   cover key and the new pad. This recovers your Part 6 new pad, digit for
   digit.
3. They copy it out onto sheets the same way you did (25 full sheets, last 2
   lines thrown away), and now hold the identical new pad you do.
4. They destroy their own copies of Sheet I and Sheet J.

Both of you now hold an identical, brand-new, secret pad — 25 sheets' worth —
without that pad ever having been sent anywhere in a form a third party could
read.

---

## The collision-bump rule, made plain

If Part 4's bumping rule wasn't clear, here is a small, made-up example with
only three lines, showing nothing but the checksum column:

```
Line A's checksum comes out to:  45678   ->  45678 is free  -> Line A keeps 45678
Line B's checksum comes out to:  45678   ->  45678 is TAKEN (by Line A)
                                          ->  try 45679  ->  free -> Line B gets 45679
Line C's checksum comes out to:  99999   ->  99999 is free  -> Line C keeps 99999
Line D's checksum comes out to:  99999   ->  99999 is TAKEN (by Line C)
                                          ->  try 00000 (wrapped around) -> free -> Line D gets 00000
```

Nothing about the line's 25 digits changes — only its sorting number does. This
will happen a few times in every real 504-line job; it is expected, not an
error.

---

## Practice drill — a small copy of the whole job, fully worked out

Doing this for real means 10 lines per sheet and 252 combinations — enough
work to take real time and concentration. Before you do it for real, practice
on this **much smaller** copy of the exact same procedure, using only 4 lines
per sheet and 6 combinations, so you can check every one of your own answers
against the ones printed here.

**This practice drill is only for learning the steps. A 4-line, 6-combination
pad is far too small to keep anything secret. Never use it for a real
message.**

### Practice sheets

```
Sheet I:
  Line 0:  39471 85062 71834 09516 27384
  Line 1:  81029 34756 80129 34756 01928
  Line 2:  47562 01938 47562 01938 47562
  Line 3:  01928 37465 01928 37465 01928

Sheet J:
  Line 0:  72610 98345 72610 98345 72610
  Line 1:  19283 74650 19283 74650 19283
  Line 2:  65039 17284 65039 17284 65039
  Line 3:  28475 61930 28475 61930 28475
```

### Practice combination list (stands in for Appendix A — pick any 2 of the 4 lines)

```
1: 01     2: 02     3: 03     4: 12     5: 13     6: 23
```

### Part 1 & 2 — expand both sheets (subtract the two named lines from zero)

```
Sheet I expanded:
  1 (01) -> 90610 91392 59157 77848 82808   checksum 89695
  2 (02) -> 34177 24110 92714 00666 46264   checksum 86711
  3 (03) -> 70711 98683 38358 74139 82808   checksum 42579
  4 (12) -> 82529 75426 83429 75426 62620   checksum 67300
  5 (13) -> 28163 49999 29063 49999 08264   checksum 23168
  6 (23) -> 62620 72717 62620 72717 62620   checksum 20284

Sheet J expanded:
  1 (01) -> 29217 48115 29217 48115 29217   checksum 43851
  2 (02) -> 73461 05581 73461 05581 73461   checksum 19245
  3 (03) -> 10025 51835 10025 51835 10025   checksum 32625
  4 (12) -> 36898 29276 36898 29276 36898   checksum 36816
  5 (13) -> 73452 75520 73452 75520 73452   checksum 59296
  6 (23) -> 27606 32996 27606 32996 27606   checksum 25680
```

Check a couple of these yourself with the no-carry rule before moving on. For
example, Sheet I entry 1 (`01`, meaning Line 0 minus Line 1 from zero, both
subtracted from the zero line): `00000 - 39471... - 81029... = 90610...` — walk
it column by column and confirm you land on the same digits.

### Part 3 & 4 — pool all 12 lines and sort by checksum

No two checksums collided in this particular practice run, so the bump rule
isn't needed here (see the separate example above for that). Sorted smallest
to largest:

```
 1.  19245  (Sheet J, combo 02)  73461 05581 73461 05581 73461
 2.  20284  (Sheet I, combo 23)  62620 72717 62620 72717 62620
 3.  23168  (Sheet I, combo 13)  28163 49999 29063 49999 08264
 4.  25680  (Sheet J, combo 23)  27606 32996 27606 32996 27606
 5.  32625  (Sheet J, combo 03)  10025 51835 10025 51835 10025
 6.  36816  (Sheet J, combo 12)  36898 29276 36898 29276 36898
 7.  42579  (Sheet I, combo 03)  70711 98683 38358 74139 82808
 8.  43851  (Sheet J, combo 01)  29217 48115 29217 48115 29217
 9.  59296  (Sheet J, combo 13)  73452 75520 73452 75520 73452
10.  67300  (Sheet I, combo 12)  82529 75426 83429 75426 62620
11.  86711  (Sheet I, combo 02)  34177 24110 92714 00666 46264
12.  89695  (Sheet I, combo 01)  90610 91392 59157 77848 82808
```

Notice the sorted list mixes lines from Sheet I and Sheet J all through it —
that is expected, and it is the whole reason for pooling both sheets before
sorting instead of keeping them separate.

### Part 5 — split in half (Table A = first 6, Table B = last 6) and add

```
  A 73461 05581 73461 05581 73461  +  B 70711 98683 38358 74139 82808  =  43172 93164 01719 79610 55269
  A 62620 72717 62620 72717 62620  +  B 29217 48115 29217 48115 29217  =  81837 10822 81837 10822 81837
  A 28163 49999 29063 49999 08264  +  B 73452 75520 73452 75520 73452  =  91515 14419 92415 14419 71616
  A 27606 32996 27606 32996 27606  +  B 82529 75426 83429 75426 62620  =  09125 07312 00025 07312 89226
  A 10025 51835 10025 51835 10025  +  B 34177 24110 92714 00666 46264  =  44192 75945 02739 51491 56289
  A 36898 29276 36898 29276 36898  +  B 90610 91392 59157 77848 82808  =  26408 10568 85945 96014 18696
```

These 6 lines are the practice **cover key**.

### Part 6 — draw fresh random lines (the practice new pad)

```
  50837 26194 50837 26194 50837
  12746 59038 12746 59038 12746
  63901 82475 63901 82475 63901
  29574 63810 29574 63810 29574
  81460 29357 81460 29357 81460
  40629 51738 40629 51738 40629
```

### Part 7 — subtract the new pad from the cover key to make the practice cover table

```
  43172 93164 01719 79610 55269  -  50837 26194 50837 26194 50837  =  93345 77070 51982 53526 05432
  81837 10822 81837 10822 81837  -  12746 59038 12746 59038 12746  =  79191 61894 79191 61894 79191
  91515 14419 92415 14419 71616  -  63901 82475 63901 82475 63901  =  38614 32044 39514 32044 18715
  09125 07312 00025 07312 89226  -  29574 63810 29574 63810 29574  =  80651 44502 81551 44502 60752
  44192 75945 02739 51491 56289  -  81460 29357 81460 29357 81460  =  63732 56698 21379 32144 75829
  26408 10568 85945 96014 18696  -  40629 51738 40629 51738 40629  =  86889 69830 45326 45386 78077
```

This 6-line block is what would be sent in the open. Anyone reading it, without
also holding Sheet I and Sheet J, cannot recover the new pad from it.

### Checking the other party's side

The other party rebuilds the same cover key (Parts 1–5, above) from their own
copies of Sheet I and Sheet J, then **subtracts** your cover table from it,
line by line, to recover the new pad. Check it yourself: subtract the first
line of the cover table from the first line of the cover key —

```
  43172 93164 01719 79610 55269   (cover key, line 1)
- 93345 77070 51982 53526 05432   (cover table, line 1)
-----------------------------------
  50837 26194 50837 26194 50837
```

That matches the new pad's first line exactly, printed under Part 6 above.
Covering used subtraction (cover key minus new pad = cover table), and
uncovering uses subtraction again, the same direction (cover key minus cover
table = new pad) — not addition.

---

## Checklist — common mistakes to watch for

- **Line numbering starts at 0, not 1.** Combination `01234` means Lines 0–4,
  the first five lines on the sheet, not lines "one through four."
- **Never carry or borrow between digit columns.** Each of the 25 columns is
  worked completely alone.
- **Work through Appendix A in order, top to bottom, without skipping.** Both
  sides must produce their 504 lines in the same order or their checksums
  (and therefore the sort, and therefore the final pad) will not match.
- **The checksum bump rule always checks earlier lines first** — Sheet I's 252
  lines are always listed ahead of Sheet J's 252 lines when checking for a
  free slot.
- **Recovering the new pad uses subtraction (cover key minus cover table),**
  the same direction as covering it. Do not add instead.
- **Only the first 25 full sheets' worth of the new pad (250 lines' worth in
  the real job) are kept; anything left over past that is thrown away,
  unused**, by both sides alike.
- **The cover table is safe to send in the clear. The new pad is not.** Do not
  confuse the two — they are different pieces of paper serving opposite
  purposes.
- **Sheet I and Sheet J are destroyed the moment this job is done**, on both
  ends, whether or not they were used correctly. They may never be reused.

---

## Appendix A — Master Combination List (252 entries)

Work through these in order, top row to bottom row, left to right within each
row. Each 5-digit code names which 5 of Sheet I's (or Sheet J's) 10 lines
(numbered 0–9) to subtract from a line of zeros for that entry.

This list is fixed and is not secret — every pair of parties doing this job
uses the exact same list, in the exact same order, every time.

```
  1: 01234  01235  01236  01237  01238  01239  01245  01246  01247  01248
 11: 01249  01256  01257  01258  01259  01267  01268  01269  01278  01279
 21: 01289  01345  01346  01347  01348  01349  01356  01357  01358  01359
 31: 01367  01368  01369  01378  01379  01389  01456  01457  01458  01459
 41: 01467  01468  01469  01478  01479  01489  01567  01568  01569  01578
 51: 01579  01589  01678  01679  01689  01789  02345  02346  02347  02348
 61: 02349  02356  02357  02358  02359  02367  02368  02369  02378  02379
 71: 02389  02456  02457  02458  02459  02467  02468  02469  02478  02479
 81: 02489  02567  02568  02569  02578  02579  02589  02678  02679  02689
 91: 02789  03456  03457  03458  03459  03467  03468  03469  03478  03479
101: 03489  03567  03568  03569  03578  03579  03589  03678  03679  03689
111: 03789  04567  04568  04569  04578  04579  04589  04678  04679  04689
121: 04789  05678  05679  05689  05789  06789  12345  12346  12347  12348
131: 12349  12356  12357  12358  12359  12367  12368  12369  12378  12379
141: 12389  12456  12457  12458  12459  12467  12468  12469  12478  12479
151: 12489  12567  12568  12569  12578  12579  12589  12678  12679  12689
161: 12789  13456  13457  13458  13459  13467  13468  13469  13478  13479
171: 13489  13567  13568  13569  13578  13579  13589  13678  13679  13689
181: 13789  14567  14568  14569  14578  14579  14589  14678  14679  14689
191: 14789  15678  15679  15689  15789  16789  23456  23457  23458  23459
201: 23467  23468  23469  23478  23479  23489  23567  23568  23569  23578
211: 23579  23589  23678  23679  23689  23789  24567  24568  24569  24578
221: 24579  24589  24678  24679  24689  24789  25678  25679  25689  25789
231: 26789  34567  34568  34569  34578  34579  34589  34678  34679  34689
241: 34789  35678  35679  35689  35789  36789  45678  45679  45689  45789
251: 46789  56789
```

That is all 252 entries. When you have worked through the whole list for both
Sheet I and Sheet J, you have completed Parts 1 and 2 of the job.
