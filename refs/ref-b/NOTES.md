# ref-b working notes (handover state)

Author: blind ref-b session of 19 October 2026 (Claude agent). Read only the sandbox copy of
`docs/specification.md` v2.2, `docs/implementation-plan.md`, `docs/detailed-implementation-plan-section3.md`,
`refs/{BRIEF,README,CASES,SEALS}.md`, `refs/compare/{FORMAT.md,compare.py}`, `refs/vectors/README.md`,
`refs/ref-b/README.md`, `refs/vectors/golden-inputs.json` and the generated T1-T3 input files.
Nothing else (no `src/`, `checks/`, `tools/`, `tests/`, `refs/ref-a/`, `refs/oracle/`, golden answers,
history). Not sealed, not committed: the coordinator copies this directory and seals it.

## State: complete for the M0 scope

Implemented (all in `src/`, see README.md for the file map):
- Conventions (spec §3): `p[v]=v^p`, `(pq)[v]=q[p[v]]`; action ATOM-TRANSPORT-1 (§2.1) on atoms,
  literals, tuples, sets, multisets, permutation leaves (g⁻¹pg), subgroup leaves (g⁻¹Hg),
  labeling-coset leaves (g⁻¹Hr), graphs (tag 09) and named relations (tag 0a).
- Objects: `subset`, `tuple`, `graph`, `dag` (tags 1-10 all parsed and encoded). The kind used by
  P1 (top-level subset / graph / other) is derived from the normalised value (QUESTIONS Q7).
- CDAG-2 (§4.1) and §4.2 normalisation (heights, per-height sort of exact record bytes, set
  dedupe, multiset count addition, unreachable discarded, cycles rejected); fast encoders for
  subsets/tuples/graphs give byte-identical streams to the generic normaliser (tested).
- Orders: CDAG-BYTE-1 (stream bytes), SIMPLE-UPPER-1 key (§4.4, U32(n) only for n≤1).
- Group kernel: deterministic Schreier-Sims (own code, `build_bsgs`), full chain on base
  0..n-1, membership, orders (BigInt), point stabilisers, least element of a coset H·w,
  §8.1 enumerator (iterative; t_b = least element of H_a u_b), §9.4 Group(H) (rule 1 by order =
  Π orbit factorials; rule 2 greedy least element of H\K by constrained-coset descent), Perm.
- P1 (§7.1-7.2), unpruned: initial keys, O stage (graph signatures, label-major then cell, (out,in)
  pairs, QUESTIONS Q8), G stage (lexmin F^G by the §7.2 constructive procedure, G_M orbits via a
  per-solve stabiliser trie keyed by M-prefixes), split, sweeps until no change, target cell
  (size, position), children [{a}, C∖{a}], leaf t_L, objective (trace, bytes) with trace first.
- Objectives 1-7 (§3, §3.1, §8.1-8.4), deterministic witness of spec v2.2 §3 for each; signed via
  the lifted group on Ω⊔{+,−} (validity = equal orders), zero iff odd element of A; signed
  singleton-orbit route (A=G, nothing enumerated, any odd generator ⇒ zero, least odd element of
  G = least element of G⁺a); signed labeling, objectives 8/9 → UNSUPPORTED_ACTION.
- Quota §11.1, work policy 1: one unit per `visit()` call of §8.1 (internal and leaf, QUESTIONS Q2)
  and per P1 NODE; the root shortcuts (singleton orbit; discrete root, Q3) count 0 P1 nodes;
  CAPACITY_LIMIT iff total > quota (Q1). Deterministic stabiliser enumerations are charged in
  full up front (count = Σ_k Π_{i≤k} r_i); transporter searches are charged per visit.
- Runner `run.jl` and FORMAT records (empty trace/bytes, witness `-`, group `-` on non-COMPLETE).

Design decisions worth knowing:
- The trace on the singleton-orbit route (P1 counts 0 there) is computed by following one child
  per node: when every generator fixes x, A=G, and after the last sweep every cell lies in one
  G_F-orbit (G stage), so all children are automorphic and every leaf has the same trace and
  image; the result equals the unpruned evaluator's. This is a computation shortcut only, never
  used when the P1 count matters.
- Transporters in deterministic mode (and TRANSPORTER_COSET always) return CAPACITY_LIMIT at once
  when full_visits(G) > quota, since the count is ≥ that in every case (computation shortcut).
- Signed enumeration runs over the lifted group (degree n+2, base 0..n+1); with a trivial kernel
  its visits are the lifts of the §8.1 enumeration of G (same count), χ(r) read off point n.
- Integers from JSON are exact; multiplicities are BigInt in encodings (Nat); O-stage counts use
  Int with an overflow guard (CAPACITY_LIMIT beyond it; never reached by the corpus).

## Tests (all pass)

- `tests/runtests.jl` (≈15 s, 7058 checks): convention identities derived from the definitions
  (array direction, product order, inverse of a product, transport p q⁻¹, cycle conjugation);
  Schreier-Sims order/membership, least coset element, §8.1 leaves = G and visit count, point
  stabilisers, Group(H) invariance under reordered/redundant/inverted generators and the rule-2
  greedy sequence, all against brute-force closure; §4.2 sharing/duplication/unreachable/insertion
  order, multiset combination, deep sharing, cycle rejection, coset-leaf action; for random small
  cases of objectives 1,2,4,5,7: identical records under generator reordering and redundancy,
  invariance of trace and bytes under x→x^h, sign covariance s(x^h)=χ(h)s(x), witnesses = least
  brute-force solutions, transporters (hit, other shape); quota boundary (Sym(4): 41 visits).
- `tests/check_corpus.jl CASEFILE OUT.tsv [MAXORDER] [STRIDE]`: definitional brute-force check
  of COMPLETE records on corpus cases with |G| ≤ MAXORDER (incl. re-solving on x^h through a
  re-ordered DAG input). Run: T1 every 2nd case (16 348 checked), T2 all |G|≤5000 (9 435),
  T3 all |G|≤5000 (1 537): 0 bad. Traces are not oracle-checkable; they were checked by hand
  against the §7.4 printed values only.
- `tests/handchecked-inputs.json`: own inputs for the other §7.4 examples (signed zero/±1, CDAG-BYTE-1
  minimum vs P1, Group(1), Group(Sym(2)), Group(C₃), labeling-coset payload, typed labeling λ=ρ,
  μ-renamed source); outputs compared by hand with the spec text: all agree.

## Runs (Julia 1.11.7, `nice -n 19`, one thread)

| File | Cases | Time | Statuses |
|---|---|---|---|
| golden-inputs.json (`golden-p1.tsv`) | 6 | ~3 s (JIT) | 6 COMPLETE; traces, streams, witnesses agree with §7.4 by hand-reading |
| T1-seed20261013.json | 34 091 | 13 s | 33 061 COMPLETE, 1 030 INVALID_INPUT (all objective 7: signs define no character) |
| T2-seed20261013-count200.json | 11 331 | 2 min 30 s | 10 683 COMPLETE, 648 INVALID_INPUT (all objective 7, same cause) |
| T3-seed20261013-count50.json | 1 900 | 43 s | 1 598 COMPLETE, 302 CAPACITY_LIMIT |

Tier outputs were written outside this directory (scratch `work/out/T{1,2,3}.tsv`, not part of the
deliverable; regenerate with the README command). compare.py on the files against themselves:
all AGREE; `--plant` reports PLANT DETECTED.

## What remains

- Comparison with ref-a, divergence review (WP0.8), seal (coordinator).
- Not in M0 scope: objectives 8/9, signed labeling (σ_ρ), work policies other than 1, the §3.2 flags.
- Possible speed work if larger tiers come: P1 subtree reuse under known automorphisms (would
  need care: counts must stay those of the unpruned tree).
