#!/usr/bin/python3

# -*- coding: utf-8 -*-

# ########################################### #
# Released under the BSD Zero Clause License  #
# ########################################### #

"""broadcast_rekeying_demo.py

A one-to-many generalization of the do_joinKeys()/do_unjoinKeys()
key-sharing scheme: instead of HQ drawing a fresh random pad `R` and
masking it with ONE pair's combinatorial key `K` before transmitting
(`ct = K - R`), HQ broadcasts one shared pile of pure entropy `B` openly,
and each of N agents -- each holding their own, independently generated
`(ki, kj)` -- locally combines `B` with their own `K_agent` to derive
their own personal replenished pad. HQ, which holds a copy of every
agent's `ki`/`kj` (that's what makes it HQ), can compute every agent's
resulting pad the same way, without needing a separate transmission per
agent.

This reuses compute_K() and shift_sheet() from keysharing_attack_demo.py
rather than reimplementing them -- same checksum-interleave-and-add
replica of otp.do_joinKeys()'s deterministic core, same null-space
transform, cross-checked there against the real otp.py.

  PART 1 - N independent agents, one shared broadcast B: each agent's
           derived pad is different, despite all starting from the same
           public digits.

  PART 2 - HQ-side recomputation: HQ, holding copies of every agent's
           ki/kj, reproduces every agent's derived pad exactly, without
           any additional per-agent transmission -- this is what makes
           "one broadcast, many recipients" actually work operationally.

  PART 3 - compromising one agent completely (their ki/kj seized, K and
           derived pad known to the adversary) gives no information
           about any other agent's K or derived pad -- independently
           generated secrets stay independent.

  PART 4 - even an agent's OWN ki/kj remain unrecoverable from total
           compromise of their derived pad, exactly as in
           keysharing_attack_demo.py Part 4 (the even-shift null space
           survives this variant unchanged, since it's a property of
           compute_K() alone, independent of what gets combined with K
           at the end).

Run directly:

    python broadcast_rekeying_demo.py
"""

import random

import otp
import keysharing_attack_demo as ksd


def hr(title):
    print()
    print('=' * 78)
    print(title)
    print('=' * 78)


def derive_pad(ki, kj, B):
    """One agent's (or HQ's, on that agent's behalf) side of the
    broadcast: combine this agent's own secret K with the shared public
    broadcast B. Mirrors ct = K - R from otp.do_joinKeys(), with B taking
    ct's public role and K_agent taking K's secret, agent-specific role."""
    K_rows = ksd.compute_K(ki, kj)          # 252 rows, this agent's alone
    K_joined = ''.join(K_rows)
    return otp.stringSubtract(K_joined[:len(B)], B)


def part1_independent_agents(agent_names, B):
    hr('PART 1: N independent agents, ONE shared broadcast B')
    print(f'shared broadcast B ({len(B)} digits, heard by everyone -- agents and any adversary):')
    print(f'  {B}')
    print()

    agents = {}
    for name in agent_names:
        ki = otp.getRandDigits(250)
        kj = otp.getRandDigits(250)
        pad = derive_pad(ki, kj, B)
        agents[name] = (ki, kj, pad)
        print(f'{name}: derived pad = {pad}')

    all_different = len({agents[a][2] for a in agents}) == len(agents)
    print()
    print(f'All {len(agents)} derived pads are distinct, from the identical broadcast: {all_different}')
    return agents


def part2_hq_recomputation(agents, B):
    hr('PART 2: HQ recomputes every agent\'s pad from its own records -- no per-agent transmission needed')

    all_match = True
    for name, (ki, kj, true_pad) in agents.items():
        hq_pad = derive_pad(ki, kj, B)   # HQ holds a copy of ki/kj -- same computation, independently
        match = hq_pad == true_pad
        all_match = all_match and match
        print(f'{name}: HQ-recomputed pad matches agent-derived pad: {match}')

    print()
    print(f'HQ reproduces all {len(agents)} agents\' pads from ONE broadcast: {all_match}')
    print('-> this is what makes "one signal, many independent recipients" work: HQ')
    print('   never needs to send anything agent-specific at all.')


def part3_one_agent_compromised(agents):
    hr('PART 3: agent1 fully compromised -- does it expose anyone else?')

    names = list(agents.keys())
    compromised = names[0]
    ki_c, kj_c, pad_c = agents[compromised]
    print(f'{compromised}: ki/kj seized, K and derived pad now fully known to the adversary.')

    K_compromised = ksd.compute_K(ki_c, kj_c)[0]
    for other in names[1:]:
        ki_o, kj_o, pad_o = agents[other]
        K_other = ksd.compute_K(ki_o, kj_o)[0]
        print(f'  K({compromised})[0] == K({other})[0]: {K_compromised == K_other}   '
              f'(independently generated ki/kj -> no relationship, regardless)')

    print()
    print('Losing one agent completely -- to capture, coercion, or a device seizure --')
    print('gives literally nothing about any other agent\'s key material, because each')
    print('agent\'s (ki, kj) was generated independently in the first place. This is a')
    print('property of how the sheets were generated, not of the broadcast scheme --')
    print('but the broadcast scheme is what makes it possible to rekey everyone from')
    print('one signal without that independence ever having to be traded away.')


def part4_agent_seed_sheets_still_hidden(agents):
    hr('PART 4: even that ONE compromised agent\'s ki/kj are not uniquely determined')

    names = list(agents.keys())
    compromised = names[0]
    ki, kj, pad = agents[compromised]

    rng = random.Random(99)
    ki2 = ksd.shift_sheet(ki, rng)
    kj2 = ksd.shift_sheet(kj, rng)

    K_true = ksd.compute_K(ki, kj)
    K_alt = ksd.compute_K(ki2, kj2)
    print(f'{compromised}: ki2 == ki: {ki2 == ki}   (deliberately different, per-column even shifts)')
    print(f'K(ki, kj) == K(ki2, kj2), all 252 rows: {K_true == K_alt}')
    print()
    print('Exactly the result from keysharing_attack_demo.py Part 4, unchanged by the')
    print('broadcast variant: this is a property of compute_K() alone, independent of')
    print('what it later gets combined with (a fresh per-pair R there, a shared')
    print('broadcast B here). Even total compromise of one agent\'s ENTIRE derived pad')
    print('does not hand back that agent\'s true ki/kj: 5**25 candidates survive for')
    print('each seed sheet, same as before.')


def main():
    otp.init()

    agent_names = ['agent1_france', 'agent2_italy', 'agent3_balkans']
    B = otp.getRandDigits(250)   # one broadcast row's worth, for a readable demo;
                                  # the real thing would be sized to a full K (6300 digits)

    agents = part1_independent_agents(agent_names, B)
    part2_hq_recomputation(agents, B)
    part3_one_agent_compromised(agents)
    part4_agent_seed_sheets_still_hidden(agents)


if __name__ == '__main__':
    main()
