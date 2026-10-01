# Slice S3 implementation notes

Brief: [`S3.md`](S3.md). Normative text: `docs/specification.md` v2.0 (cited as §x). Prerequisites: S1 ([`S1-notes.md`](S1-notes.md)) and S2 ([`S2-notes.md`](S2-notes.md)); their readings still apply. This file records what was built, the readings taken, the construction policies as implemented, the measured counters, the deviations from the brief and what was left out.

**Order of writing.** `src/bsgs/verify.c` was written first, before any line of `src/bsgs/chain.c` existed. Only the data layout in `chain.h`, `perm.h` and `provenance.h` existed at that point, and the verifier uses nothing else. The history shows this: commit `c1a0bd1` adds the layout, the provenance records and the verifier, and `60eaf90` adds the constructor. The verifier has its own transporter reconstruction and its own sift; it calls no function of `chain.c`.

## Files added

| Path | Content |
|---|---|
| `src/bsgs/chain.h`, `chain.c` | Chain data (brief §2.1), the deterministic constructor (§9.1 second paragraph, policies below), transporter reconstruction, sift, allocation-free membership, suffix (point-stabiliser) orders, `rebase` with verification, §7.1-ordered orbit ids of a pointwise stabiliser, §7.2 tuple minimum, counters (`canon_bsgs_stats`) |
| `src/bsgs/verify.h`, `verify.c` | The independent verifier (§9.1 third paragraph, brief §2.3), with one reason per violated condition |
| `src/bsgs/provenance.h`, `provenance.c` | Straight-line records `INPUT i \| INVERSE j \| PRODUCT j k` (§9.2), evaluated in one forward pass with malformed records reported |
| `src/bsgs/chain_backend.h`, `chain_backend.c` | `canon_group_ops` over a verified chain, the default backend |
| `src/bsgs/reference.h`, `reference.c` | The direct Schreier recursion of §9.1 first paragraph, for tests only |
| `tests/c/t1_groups.h` | The 40 T1 subgroups of `Sym(n)`, `n ≤ 4`, with greedy generators |
| `tests/c/test_chain.c` | Product sides; T1 and T2 against the explicit backend; `Sym(12)`; 64-cycle; capacity; API |
| `tests/c/test_verify.c` | Ten mutations, further reasons, the non-nested counterexample |
| `tests/c/test_provenance.c` | Record kinds, re-derivation of every strong generator, DAG bound |
| `tests/c/test_reference_schreier.c` | §9.1 recursion against the chain on T1 |
| `docs/slices/S3-notes.md` | This file |

Files changed:

- `src/perm/perm.h`, `perm.c`: `canon_perm_table`.
- `src/arena/checked.h`: `canon_u32_grow`, the one saturating capacity-doubling rule used by the grow-only tables.
- `src/bsgs/explicit.h`, `explicit.c`: `canon_group_is_explicit`.
- `include/canon/canon.h`: `canon_backend`, `canon_context_set_group_backend`, `canon_group_order`, and documentation of the capacity changes.
- `src/api/api.c`: the backend in the context, dispatch in `canon_group_create`, `canon_group_order`, and `max_group_order` applied to explicit groups only.
- `tools/canon-cli.c`: `--backend chain|explicit`.
- `tests/c/test_search_subset.c`: see the deviations.
- `tests/python/test_e2e.py`: `CANON_BACKEND`, and the `Backends` class.
- `Makefile`, `CMakeLists.txt`: new sources and tests; the explicit e2e run in `make check` and as ctest `test_e2e_explicit`.
- READMEs of `include/`, `src/`, `src/{api,bsgs,perm}/`, `tools/`, `tests/c/`, `tests/python/`.

`.github/workflows/ci.yml` is unchanged. ctest picks up the new C tests and `test_e2e_explicit` from `CMakeLists.txt`.

## Spec readings taken

1. **Queue order (§9.1).** "build the orbit of a by queue traversal in increasing discovered-label order". Read as FIFO: points are expanded in the order of their discovery index (the orbit position, the "label" a point receives when discovered). Generators are taken in `gen_ids` order (insertion order) in the constructor, and in sorted order in the reference recursion, as §9.1 says for it. The reading does not change any output. Order, membership, the tuple minimum and orbit ids are invariants of the group. It only shapes the Schreier trees.
2. **Schreier vector (brief §2.1, plan §2.3).** `parent_gen` is an index into the level's `gen_ids` (plan §2.3: "index into level generators"), so a stored edge can only use a level generator. `parent_point` is a point. Both are indexed by orbit position.
3. **Inclusive membership.** Brief §2.1 says "levels[i] generates G_(b_0..b_{i-1})". A strong generator inserted at level `j` fixes `b_0..b_{j-1}` and is listed in `S_0, …, S_j`, so `S_{i+1} ⊆ S_i`. This is the usual BSGS convention `S^(i) = S ∩ G_(b_0..b_{i-1})`.
4. **Provenance and rebase (§9.1 "each generator's derivation from the original input", §9.2).** A chain's `INPUT` records name the generators it was built from: the validated user input for a group's chain, or the strong generators of the source level for a rebased chain. The verifier checks the chain's record of its inputs against the caller's originals. A rebased chain is therefore derived from its source's verified strong generators, and those are derived from the user input. Copying the source DAG into every transient chain of a tuple minimum would only add cost.
5. **Order beyond uint64 (§9.2 "Group order is an exact multi-limb product", §11.1, detailed plan §2.1 count-bit limit 64).** Groups of order above `2^64 − 1` are `CAPACITY_LIMIT` at `canon_group_create` (brief §2.5). During construction, inclusive membership gives `K_(i+1) ≤ Stab_(K_i)(b_i)`. The product of the orbit lengths is therefore a lower bound on `|G|` at every stage, and it equals `|G|` at the end. The constructor stops as soon as that product overflows. This is the case iff `|G| > 2^64 − 1`, so the outcome is a function of the group alone. It also bounds the work: at most 63 levels with a nontrivial orbit, and at most 63 insertions per level.
6. **Tuple minimum (§7.2) and the least minimiser.** The procedure runs over the list `L` followed by `0, 1, …, n−1`. After `|L|` steps, `t` minimises `L^t` and the minimisers are exactly `t·G_M` with `M = L^t`. The orbit ids of `G_M` are read at that point. The remaining steps minimise `(t g)[0], (t g)[1], …` over `g ∈ G_M`, which is the least minimiser that the group interface (S1) and the explicit backend return. Each step needs `H_b`. When `b` is not the first base point of the current `H` chain, `H` is rebased with prefix `(b)` and verified, and `u = t_a⁻¹` is taken from that level (`t_a` sends `b` to `a`). The brief's "u = t_b of that level" with base `a` would need `H_b = u⁻¹ H_a u` as a second step; both are one rebase per step. The cost is at most `n` verified rebuilds per call. A step is skipped without any rebuild when `a` is fixed by `H`, and it uses the current level when `b` is already its base point.
7. **Membership without allocation.** `contains` returns `bool`, and the interface is unchanged. Membership therefore evaluates the residue point by point. It keeps at most 63 (level, point) pairs on the stack, which is enough because the order fits `uint64`. A heap allocation would have needed a failure value that `bool` cannot carry.
8. **What `TRANSVERSAL_IMAGE` checks.** No transporter is stored, so condition 6 checks the verifier's own reconstruction `t_b = s_0 … s_(k-1)` against the stored edges. Once the edges have passed condition 4, no data mutation can make it fail. It pins the product side, as the brief intends. The ten mutations therefore do not include it.

## Construction policies as implemented (brief §2.2, plan WP2.3)

1. **Normalisation.** Identities and exact duplicates of an earlier input are dropped, and first occurrences keep their order. Each kept input `i` gets the record `INPUT i`; indices refer to the original input list. Each kept input is sifted from the root.
2. **Base extension.** A residue that fixes every base point and is not the identity appends its least moved point.
3. **Insertion level.** The level at which the residue's sift stopped, i.e. the deepest level whose prefix it fixes (or the new level of rule 2). The generator joins `S_0..S_j` (reading 3).
4. **Rebuild and closure.** After an insertion at level `j`, the orbits, Schreier vectors and `orbit_pos` of levels `0..j` are recomputed. Those are the levels whose generator lists changed; see the conflict below. The closure then sifts the Schreier generators `t_b s t_(b^s)⁻¹`:
   - level by level from `j` down to `0` (deepest first), each level in orbit order then generator order, through the levels below;
   - a Schreier generator that is a tree edge (`b^s` was discovered from `b` through `s`) is the identity by construction; it counts as a candidate but is not sifted;
   - a nonidentity residue is inserted (rules 2 and 3), and the pass restarts at its insertion level, the deepest affected level;
   - the closure ends when a pass finishes level 0 without inserting.

   Every pass starts with an insertion, so the number of passes equals the number of insertions.
5. **Strict growth.** Only the nonidentity remainder of a sift is inserted. The sift stopped at level `j` because the residue's image of `b_j` lies outside `b_j^(K_j)`. Hence the residue is not in `K_j`, and `K_j` grows strictly.
6. **Provenance.** Records are made only for inserted generators. An inserted Schreier residue is recorded as follows:
   - `t_b` is built by left multiplication of the generator records along its tree path;
   - then `PRODUCT` with `s`;
   - then `PRODUCT` with the stored inverse records along the path of `b^s`, in upward order (this is `t_(b^s)⁻¹`);
   - then the same for every sift step;
   - then one `INVERSE` record for the stored inverse.

   Long words are never expanded.

**Verifier.** It checks, in this order, and stops at the first violation:

- structure (ranges and ids);
- 9, terminal triviality;
- 1, bijections of the inputs, generators and inverses;
- 2, recorded inputs equal the originals, every record re-derives its stored array, and every inverse is the inverse;
- 3, prefix fixation;
- nesting;
- 4, root, duplicates, `orbit_pos` inversion, stored edges, and reachability (cycles detected);
- 5, closure;
- 6, transversal images;
- 7, Schreier residues;
- 8, input membership;
- 10, order.

## Measured counters (no timings)

From `test_chain` (printed on every run):

| Set | Groups | Candidates | Sifts | Insertions | Passes | Dense compositions |
|---|---|---|---|---|---|---|
| T1, greedy generators | 40 | 366 | 244 | 62 | 62 | 567 |
| T2, 200 seeded random sets, `n ≤ 8`, 1–3 generators each a uniform permutation or a single random cycle | 200 | 11945 | 8865 | 572 | 572 | 40904 |
| `Sym(12)` from `(0 1)` and the 12-cycle | 1 | 1895 | 1693 | 18 | 18 | 8228 |

Provenance DAG sizes, from `test_provenance` (its T2 set is 200 seeded sets of 1–3 uniform random permutations):

| Set | Chains | Nodes | Insertions | Largest DAG | max nodes / (insertions · n) |
|---|---|---|---|---|---|
| T1 | 40 | 135 | 62 | 12 | 1.000 |
| T2 (uniform) | 200 | 1879 | 563 | 45 | 1.167 |
| `Sym(12)` and the 64-cycle | 2 | 68 | 19 | 66 | 0.306 |

The test asserts two bounds:

- The proved bound `nodes ≤ kept_inputs + insertions · (2n + Σ_j r_j + 1)`, where `r_j` are the final orbit lengths. The derivation is in the test's header comment. It is polynomial because `Σ_j r_j ≤ n · depth`.
- The brief's example bound `nodes ≤ 4 · insertions · n` on these fixed sets. It holds there, but it is measured, not proved.

## Deviations from the brief and why

- **Two S1 assertions now select the explicit backend** (`tests/c/test_search_subset.c`). Brief §2.5 makes `max_group_order` apply to the explicit backend only. §4 asks for "All S1 and S2 tests pass unchanged". Two S1 assertions test `max_group_order` with the default backend: the builder refusing `Sym(2)` under a context limit of 1, and the problem-time check with `max_group_order = 1`. They cannot pass unchanged under §2.5, so they now create their groups from an explicit-backend context. Next to them, new assertions check that the chain backend admits the same groups. Nothing else in the S1 and S2 tests changed.
- **Order overflow** (reading 5). The constructor stops early instead of storing a chain marked "exceeds uint64". No such chain is ever produced, and the verifier rejects a product that does not fit (`ORDER_MISMATCH`).
- **Recomputation of levels `0..j`** after an insertion at `j`, instead of "levels ≥ i" (see the conflict below).
- **A nesting check in the verifier** beyond the brief's ten conditions (see the conflict below).
- **Tuple-minimum step** (reading 6): rebase at `b` and `u = t_a⁻¹`, rather than rebase at `a` and `u = t_b`.
- **Sparse permutations and a tagged identity in `src/perm/`** (brief §1, "otherwise document why dense suffices"). Dense suffices for S3:
  - The chain never stores the identity (normalisation drops it, identity residues are never inserted, and base-point transporters are implicit). This is the tagged identity in the form S3 needs.
  - The chain stores at most two dense rows per strong generator, and there are at most `63 · depth` insertions with `depth ≤ 63` beyond a rebase prefix.
  - Levels hold `4n` words each.
  - The sizes exercised (`n ≤ 64` in tests, `n ≤ 34` end to end) make sparse support a performance question, which belongs to M5. Spec §9.3's examples (`n = 100,000`, cached transversals) are also performance budgets.
- **`canon_bsgs_stats` passes.** Passes equal insertions by construction (policy 4), so the counter carries no extra information under these policies. It is kept for M5, where other policies may differ.

## Suspected conflicts (spec, brief, model)

1. **The §9.1 verifier list is not sufficient without nesting.** Spec §9.1: "The independent verifier checks input bijections, each generator's derivation from the original input, base-prefix fixation, orbit reachability via stored tree edges, orbit closure under level generators, transversal images, all Schreier residues' membership in the certified next subgroup, input-generator membership at the root, and terminal triviality. Induction gives both inclusions at every level and completeness." Brief §2.3 lists the same conditions plus the order.

   Concrete case: `Sym(3)` from inputs `(0 1)`, `(1 2)`, with these levels:

   | Level | Base | Generators | Orbit |
   |---|---|---|---|
   | 0 | 0 | `S_0 = {(0 1)}` | `{0, 1}` |
   | 1 | 1 | `S_1 = {(1 2)}` | `{1, 2}` |
   | 2 | – | none (terminal) | – |

   Stored order: 4. Every listed condition holds:
   - both levels' Schreier residues are the identity;
   - both inputs sift to the identity;
   - the order is the product of the orbit lengths.

   Yet `|Sym(3)| = 6`. The induction step `K_(i+1) = Stab_(K_i)(b_i)` needs `K_(i+1) ≤ K_i`, which "the certified next subgroup" presupposes but no listed condition checks. The verifier adds that `S_(i+1) ⊆ S_i` (`CANON_BSGS_NOT_NESTED`). `test_verify.c` builds exactly this chain and requires that rejection; the constructor's chain for the same inputs has order 6 and is accepted. The proof sketch in `verify.c` shows that the listed conditions plus nesting give both inclusions. Suggest adding "level generators nested (`S_(i+1) ⊆ S_i`, or each level's generators members of the level above)" to §9.1's list and to the plan's WP2.4.

2. **Brief §2.2 item 4 recomputes the wrong levels.** Brief: "After inserting at level `i`: recompute orbit, Schreier vector and `orbit_pos` for levels `≥ i`. Then re-sift every Schreier generator of every level `≥ i`, … restarting the pass from the lowest affected level." A generator inserted at level `i` belongs to `S_0..S_i` (reading 3). It changes the orbits of levels `≤ i` and does not belong to levels `> i`.

   Concrete case: inputs `(0 1)` then `(1 2)` on 3 points.
   - `(0 1)` is inserted at level 0 (base 0, orbit `{0,1}`).
   - `(1 2)` sifts through level 0 unchanged (it fixes 0), so the base is extended by 1 and it is inserted at level 1.
   - Level 0's orbit must become `{0,1,2}`. Recomputing only levels `≥ 1` would leave it at `{0,1}`, and the order would come out as 4.

   Implemented: recompute levels `0..i`, re-sift from level `i` up to level 0, and restart at the insertion level. "Lowest affected level" is read as the deepest one, with the trivial group at the bottom. Termination ("a complete pass inserts nothing") is unchanged.

3. **Brief §4 versus §2.5:** see the first deviation.

**Spec versus model.** No disagreement between the specification and `checks/review_checks.py` was found in the S3 scope. Evidence:
- Every tier of `test_e2e.py` agrees with the model byte for byte under both backends:
  - T1 subsets: 1078 runs;
  - G1: 664 runs;
  - G2: 2880 runs;
  - the golden cases, quotas and statuses.
- On the chain side, the T1 and T2 comparisons with the explicit backend in `test_chain.c` are exact: order, membership of every element of `Sym(n)`, `tuple_min` `t` and orbit ids for every list length, and rebases.

The model has no Schreier construction, so §9.1 itself is checked only against the explicit backend and the reference recursion.

## Left out (later slices)

- The coset enumerator and least element with constraints (S4). Canonical `Group(H)` bytes (S4). Multi-limb orders: groups beyond `uint64` are `CAPACITY_LIMIT` until S4.
- Randomised construction.
- Performance work on construction, compact Schreier trees, dense transporter caches and sparse permutations (M5). The rebase copies a level's strong generators and rebuilds without reusing existing levels (reuse is optional in §9.2).
- The tuple minimum allocates its scratch per call (a shared immutable group cannot hold per-call state) and verifies every rebased chain. That is up to `n` verified rebuilds per G-stage or leaf call. It is acceptable at the sizes of the e2e tiers, and a candidate for M5.
- `equal` and `subgroup_of` by mutual sifting (plan WP2.5): nothing in S3 needs them.

## Timing and environment

- `make check` without sanitizers: 15.9 s wall-clock. This is `review_checks.py`, the 31 Python tests with the chain backend (about 7.4 s), then the 14 tests of `test_e2e.py` again with the explicit backend (about 7.7 s). `test_chain` takes about 0.5 s; under ASan/UBSan it takes about 1.7 s.
- The CMake sanitizer build (`-DCANON_SANITIZE=ON`) passes all 21 ctest entries. `test_e2e` takes about 56 s and `test_e2e_explicit` about 53 s.
- As in S1 and S2, the local clang has no ASan runtime, so the clang check is a plain build: `make CC=clang BUILD=build/clang`, zero warnings, all 18 C tests pass.
