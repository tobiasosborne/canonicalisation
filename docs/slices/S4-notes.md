# Slice S4 implementation notes

Brief: [`S4.md`](S4.md). Normative text: `docs/specification.md` v2.0 (cited as §x). Prerequisites: S1 ([`S1-notes.md`](S1-notes.md)), S2 ([`S2-notes.md`](S2-notes.md)) and S3 ([`S3-notes.md`](S3-notes.md)); their readings still apply. This file records what was built, the readings taken, the deviations from the brief, the measured counters and what was left out.

## Files added

| Path | Content |
|---|---|
| `src/coset/coset.h` | Interface of the coset module: point constraints, counters, the visitor (consumer, cancellation poll, quota, counters), the shared node-entry rule |
| `src/coset/least.c` | `canon_coset_least` (least element of `J r` under point constraints, §9.4, §8.1 `t_b`) and `canon_coset_least_outside` (least element of `H \ K`, §9.4 rule 2) |
| `src/coset/enumerate.c` | The §8.1 enumerator over a chain, zero pruning, quota per visit call, cancellation poll per node |
| `src/encoding/group_stream.h`, `group_stream.c` | `Perm(p)` (§4.1), `Group(H)` rules 1 and 2 (§9.4), the coset payload `Group(H) \|\| Perm(r₀)`, a conservative output bound (§11.1) |
| `src/search/objectives.h`, `objectives.c` | The §8.2 consumers (minimum under both orders, transporter one, stabiliser, transporter coset), the §3 deterministic witness, the §17 witness check |
| `tests/c/test_coset_least.c` | Product side; T1 and random groups against brute force; rule 2 on every T1 pair `K ≤ H`; invalid arguments |
| `tests/c/test_enumerate.c` | Product side by a hand-derived `Sym(3)` leaf sequence; exactly `G`, each once; chain against the explicit oracle (sequence and node count); quota, stop, cancellation; `C_2^17` |
| `tests/c/test_group_stream.c` | §7.4 payloads; T1 against a brute-force §9.4; presentation and base independence; `Sym(5)`, `A₄`, `D₄`; coset payloads; the bound |
| `tests/c/test_objectives.c` | Every objective through the public API; deterministic witness on T1 under both backends; `verify_witness` with tampered witnesses; validation; quota, output bound, lifetimes, workspace reuse; T1 counters |
| `tests/python/test_compare.py` | `refs/compare/compare.py` on seven-field CLI records of both backends |
| `docs/slices/S4-notes.md` | This file |

Files changed:

- `include/canon/canon.h`: `canon_witness_mode`, `canon_problem_options`, `canon_problem_create_with_options`, `canon_result_group_bytes`, `canon_result_order_key`, `canon_result_transporter`; documentation of `canon_solve`, `canon_result_witness`, `canon_result_bytes`, `canon_result_encode`, `canon_result_verify_witness`, `max_search_nodes`, and of the deferred multi-limb order.
- `src/api/api.c`: the problem builder with target and options, solve dispatch, results with group payload, order key and retained inputs, the new accessors, `canon_result_verify_witness`. `src/api/stubs.c`: the verify stub removed.
- `src/bsgs/group.h`: `canon_group_ops.enumerate`. `src/bsgs/chain.h`, `chain.c`: `canon_bsgs_build_verified`, `canon_bsgs_transporter_scratch`. `src/bsgs/chain_backend.c`: `enumerate` over the chain; creation through `canon_bsgs_build_verified`. `src/bsgs/explicit.c`: the oracle `enumerate` over the element table. `src/bsgs/chain_backend.h`: comment only.
- `src/object/object.h`, `object.c`: `canon_root_equal`. `src/encoding/simple_upper.h`, `simple_upper.c`: `canon_simple_upper_in_class`.
- `tools/canon-cli.c`: subcommands `min`, `transporter`, `stabiliser`, `transporter-coset`; `--kind`, `--target-*`, `--order`, `--witness`; seven-field records.
- `refs/compare/FORMAT.md`, `compare.py`, `README.md`: the seventh field `group_hex`.
- `tests/python/test_e2e.py`: seven-field parsers and the `EnumerationObjectives` tiers. `tests/c/test_version.c`: `verify_witness` is no longer a stub; `canon_problem_create_with_options` NULL arguments. `tests/c/test_chain.c`: one comment.
- `Makefile`, `CMakeLists.txt`: new sources and tests. `.github/workflows/ci.yml` is unchanged: ctest registers the new C tests, and the Python job's `unittest discover` (with the CLI built and `CANON_REQUIRE_CLI=1`) runs `test_compare.py`.
- READMEs of `include/`, `src/`, `src/{api,bsgs,coset,encoding,object,search}/`, `tools/`, `tests/c/`, `tests/python/`, `refs/compare/`.

## Spec readings taken

1. **Profile of the enumeration objectives (§4.3).** "Profile tag `0x0000` means `NO_TREE` for coset-enumeration objectives; canonical and signed image objectives use P1 (`0x0001`)." `LEX_MIN_IMAGE`, `TRANSPORTER_ONE`, `STABILISER` and `TRANSPORTER_COSET` therefore require profile `NO_TREE`; P1 with them, or `NO_TREE` with `CANONICAL_IMAGE`, is `UNSUPPORTED_ACTION` ("unsupported, never reinterpreted", §4.1).
2. **Orders.** `SIMPLE-UPPER-1` is "the minimum-search order" (§4.4) and is accepted only for `LEX_MIN_IMAGE`. The other objectives accept `CDAG-BYTE-1`, the order of their canonical bytes (§4.3 key tuple). Everything else is `UNSUPPORTED_ACTION`.
3. **Constrained least element (§9.4 "by successive point constraints").** The constrained points are fixed first, in increasing point order. Each such step restricts the coset to the admissible elements, which form a coset `J'' r''` again. The unconstrained descent over `v = 0..n−1` then minimises the image array inside it. This differs from the brief; see conflict 1.
4. **Group bytes of a coset result.** `canon_result_group_bytes` returns `Group(A) || Perm(r₀)` when `subgroup_verified`, `stabiliser_complete` and `witness_valid` all hold. An empty coset has `transport_exhausted` and no payload. See conflict 2.
5. **Logical work quota (§11.1).** "A logical work quota … counts the fixed reference traversal." One quota covers the whole solve, and `max_search_nodes` keeps its name:
   - For an enumeration it counts `visit` calls, leaves included. `CAPACITY_LIMIT` means the traversal needs more than `max_search_nodes` visits.
   - For `TRANSPORTER_COSET` the visits of both enumerations add up: the transporter one (up to its hit) and the stabiliser.
   - For `CANONICAL_IMAGE` in deterministic mode, the P1 NODE tokens and the stabiliser enumeration's visits add up.
   - The traversal and its stopping point are fixed, so the outcome is a function of the input, the objective, the options and the descriptor. `test_e2e.py` checks the exact boundaries against a Python model of the traversal.
6. **Transporter flags (§3.2).** "A positive transporter witness needs no negative-search coverage": a hit gives `COMPLETE` with `witness_valid` only. An empty result is `COMPLETE` with `transport_exhausted`, after exhaustion only (§8.2). `canon_result_transporter` returns the hit whenever `witness_valid` holds; the brief's "NULL if none or not exhausted" is read as "NULL if none, or if the solve did not complete".
7. **Stabiliser insertion (§8.2 "insert every hit into a verified subgroup").** Every hit is sifted through the current verified chain of `A_known`. A member is already inserted. A non-member is appended to the generators, and the chain is rebuilt and verified (`canon_bsgs_build_verified`). Each insertion at least doubles `|A_known|`, so there are at most log₂|A| rebuilds; `test_objectives.c` asserts this on T1.
8. **Equality of images.** The transporter and stabiliser consumers test `x^r = y` by extensional object equality (`canon_root_equal`), as the brief asks. `verify_witness` compares CDAG-2 streams, as the brief asks for it. The encoding is injective on normalised objects (§4.2), so the two tests agree.
9. **Witnesses (§3).**
   - Minimum, `any` mode: the first attaining leaf of the §8.1 traversal. Deterministic mode: the least attaining `g`.
   - Transporters: the first hit. For the coset in deterministic mode, `r₀`, the least element of `A g`, which is the least solution.
   - Canonical image, deterministic mode: the least element of `A t` (§3: "minimising A g after A is proved complete").
   - Deterministic mode is refused for `TRANSPORTER_ONE` (it would need the coset) and for `STABILISER` (no witness), with `UNSUPPORTED_ACTION`.
10. **Deterministic witness of the canonical image versus S1 reading 1.** Two checks show that the least element of `A t` equals the least attaining leaf witness of the unpruned tree, so neither definition is assumed:
    - `test_objectives.c`: every T1 group and subset, through the public API, under both backends (1078 solves);
    - `test_e2e.py`: T1, G1 and the random tier, against the model's `min(g : x^g = c)`.
11. **Output capacity (§11.1).** Where the output length depends on the data, the API uses the following:

    | Objective | Output checked against `max_output_bytes` |
    |---|---|
    | `CANONICAL_IMAGE`, `LEX_MIN_IMAGE` | The stream length of `x`, which is exact |
    | `STABILISER` | A conservative input-derived bound, `5 + max(4⌊n/2⌋ + 4n, ⌊log₂|G|⌋(4 + 8n))`; §9.4 gives `k ≤ ⌊log₂|H|⌋` and `|H| ≤ |G|` |
    | `TRANSPORTER_COSET` | The same bound plus `4 + 8n` for `Perm(r₀)` |
    | `TRANSPORTER_ONE` | Nothing: no canonical bytes |

    The `SIMPLE-UPPER-1` key is metadata, not canonical stream bytes, so it is not counted.
12. **Rule 1 in `uint64` (brief §3.5).** `∏|O_i|!` is computed with overflow detection. While orders fit `uint64`, an overflowing product exceeds `|H|`, so rule 1 correctly does not apply. Orders fit `uint64` because S3's admission refuses `|G| ≥ 2^64` and `A ≤ G`.
13. **Rule 2 descent (§9.4).** The pieces of the descent:
    - "C ⊆ K iff r ∈ K and J ≤ K": `J ≤ K` is tested by sifting the generators of `J`'s chain level through `K`.
    - The children of a branch share `J'_v`, so that test is made once per level.
    - Once `J' ≤ K`, the invariant "C ⊄ K" gives `r' ∉ K`, so `C ∩ K = ∅`. The answer is then the least element of `C`.
    - `K = H` is detected by equal orders, since `K ≤ H`.
    - `K` is rebuilt from `g_1..g_i` and verified after each insertion (detailed plan WP2.7), because its membership test drives the next descent.
14. **The enumerator (§8.1).** It follows the spec's pseudocode:
    - `H` is a chain suffix, rebased (transient, unverified) at `a` unless `a` is already its base point.
    - The orbit is sorted with the shared stable sort.
    - `t_b` is `canon_coset_least(H, id, {a ↦ b})`.
    - The child is `visit(H_a, t_b r)` with `t_b` acting first.
    - A level with no generators is the trivial group and is consumed.

    "Smallest atom moved by H" is the least point moved by one of the level's generators. A point moved by some element is moved by some generator.
15. **Both backends (detailed plan §0a.4).** The explicit backend implements `enumerate` independently over its sorted table:
    - `H` is a list of rows in table order, so the first matching row is the least.
    - `H_a` is the sublist of rows fixing `a`.
    - The orbit is the set of row images of `a`.

    It is the C oracle for the chain enumerator: identical leaf sequences and node counts on T1 and 100 random groups (`test_enumerate.c`).
16. **`n = 0`.** `G` is trivial, with one leaf, the empty permutation. The objectives work. The witness is the empty array, which the CLI writes `-`. `Group(1) = 01 00000000` and `Perm(()) = 00000000`.
17. **Status precedence of `canon_problem_create_with_options`.** The spec fixes no order. This one is used:
    1. NULL arguments and an unknown witness mode (`INVALID_INPUT`);
    2. unsupported combinations (`UNSUPPORTED_ACTION`);
    3. degree mismatch, missing, superfluous or mismatched target (`INVALID_INPUT`);
    4. `SIMPLE-UPPER-1` outside the §4.4 class (`UNSUPPORTED_ACTION`);
    5. capacity (`CAPACITY_LIMIT`).
18. **`verify_witness` (§17).** `*valid` holds iff the witness is a bijection, the backend's `contains` accepts it, and the stream of `x^w` equals `c`. `c` is the result's bytes for images and minima, and the target's stream for transporters. A result without a witness (stabiliser, empty transporter, incomplete) gives `INVALID_INPUT` with `*valid = false`. To verify after the problem is released, the result retains the group, object and target. These are owned references to immutable handles, not borrowed pointers.

## `canon_nat` and the order limit (brief §3.5)

The multi-limb `canon_nat` of detailed plan §2.1 is **deferred** to the slice that lifts the `uint64` order limit. Until then, §9.2's "Group order is an exact multi-limb product ∏r_i; it is not uint64 in general" is met only within `uint64`. Groups of order `≥ 2^64` are `CAPACITY_LIMIT` at `canon_group_create` (S3 reading 5), which §11.1 permits under the count-bit limit of 64. The comments in `canon.h`, `chain.h` and `chain_backend.h` that said "S4 lifts this" now point here. Rule 1's factorial product is the only other place where a larger integer could arise, and overflow decides it correctly (reading 12).

## Deviations from the brief and why

- **Constraint order in the descent** (reading 3, conflict 1).
- **`canon_group_ops.enumerate`.** The brief does not say how the explicit backend serves the enumeration objectives. A backend operation keeps the API backend-agnostic (`api.c` and `objectives.c` never look at the chain behind a group). It also makes the explicit backend a real oracle rather than a second path into the same chain code.
- **Group bytes of a coset** require `witness_valid`, not `transport_exhausted` (reading 4, conflict 2).
- **One quota per solve** across phases (reading 5). The brief left the multi-phase case open.
- **CLI.**
  - The object kind of the S4 subcommands is `--kind subset|graph`, defaulting to graph exactly when a graph option is given. Mixing subset and graph options is a usage error.
  - A target option that is not given defaults to the empty subset or the arc-free, uncoloured graph.
  - `--witness` is accepted by `p1-subset` and `p1-graph` too, so that the deterministic witness of the canonical image is tested end to end.
  - Records of every non-`COMPLETE` status keep `-` in `witness` and `group_hex`.
- **`compare.py`.** `--plant` now corrupts `bytes_hex` or `group_hex`, so files of group answers can be planted too. Format errors now exit 2 as the script documents (`sys.exit(message)` exited 1). `tests/python/test_compare.py` is new.
- **Interrupted results are status-only**, as in S1–S3. §3.2 permits but does not require a partial candidate or subgroup.
- **Cancellation** is an internal poll in the visitor, tested in `test_enumerate.c`. No public cancellation entry point exists yet (§14 runtime work).

## Suspected conflicts (spec, brief, model)

1. **Brief §3.1, interleaved constraints.** The brief says: "Descend over points v = 0, 1, …, n−1: … choose the least image c (or the constrained c_v if v is constrained, returning 'empty' if it is not among the images)". Read literally, this chooses least images at unconstrained points before later constraints are applied, and can report empty when admissible elements exist.

   Concrete case: `J = Sym(3)`, `r = id`, constraint `1 ↦ 0`.
   - At `v = 0` the least image `0` is chosen, so `J' = Stab(0)`.
   - At `v = 1` the images are `{1, 2}`, which do not contain `0`, so the descent reports "empty".
   - Yet `[1,0,2]` satisfies the constraint; the correct answer is `[1,0,2]`.

   The spec's "successive point constraints" (§9.4) has no such problem when the constraints are applied first, and that is what is implemented (reading 3). The first implementation followed the brief and failed the brute-force test (`test_coset_least.c`). For the enumerator's single constraint `a ↦ b`, with `a` the least moved point, both orders agree, because every point below `a` is fixed. Suggest rewording brief §3.1 (and detailed plan 2.4) to "apply the constraints first, then minimise".
2. **Brief §2, the coset accessor against the coset flags.** The accessor says: "`canon_result_group_bytes` … NULL/0 unless subgroup_verified && stabiliser_complete (and transport_exhausted for a coset)". The flags say: "transporter coset → the union, with `transport_exhausted` meaning the empty case was proved".

   Concrete case: §7.4's labeling-coset payload, `x = {0}`, `y = {1}`, `G = ⟨[1,0]⟩`.
   - The coset is nonempty (`A = 1`, `g = [1,0]`), so by the flag rule `transport_exhausted` is false.
   - The literal accessor rule would then never return `01 00000000 00000002 00000000 00000001 00000001 00000000`.

   Implemented: the flag rule, with the payload gated on `witness_valid` (reading 4). Suggest "(and witness_valid for a coset)" in the brief.
3. **Spec versus model.** No disagreement between the specification and `checks/review_checks.py` was found in the S4 scope. Evidence:
   - `group_bytes` and `perm_bytes` of the model equal the C payloads on every T1 group and every stabiliser and coset of the e2e tiers.
   - The model reads the orbit order as blocks by least point, the same as the implementation.
   - The §7.4 payloads match.
   - The spec's §8.1 traversal, modelled in `test_e2e.py` (`enumerate_81`) directly from the pseudocode, gives exactly the C leaf order (pinned through the `any`-mode witnesses).

## Measured counters (no timings)

From `test_enumerate` (printed on every run). Rebuilds are transient chain rebases, by the enumerator and inside the descents. Descents are calls of `canon_coset_least`, one per non-root node.

| Set | Groups | Visits | Leaves (`= Σ|G|`) | Rebuilds | Descents |
|---|---|---|---|---|---|
| T1, greedy generators | 40 | 252 | 164 | 21 | 212 |
| T2, 100 seeded random sets, `n ≤ 7`, 1–3 uniform permutations | 100 | 91260 | 53746 | 6695 | 91160 |
| `C_2^17` on 34 points | 1 | 262143 | 131072 | – | – |

From `test_objectives`: the stabiliser objective on every T1 group and subset (539 runs), with greedy generators and the chain backend.

| Visits | Leaves | Hits (`Σ|A|`) | Verified rebuilds of `A` | Enumeration rebuilds | Descents | Rule-2 rebuilds of `K` | Rule-2 descent rebuilds |
|---|---|---|---|---|---|---|---|
| 3763 | 2431 | 1133 | 368 | 328 | 3224 | 98 | 18 |

From `test_group_stream`: the 40 T1 groups encode 24 by rule 1 and 16 by rule 2. Of 60 random groups (`n ≤ 7`, permutations and transpositions), 13 encode by rule 2. Every rule-2 payload satisfies `2^k ≤ |H|`.

The descent's cost model is the S3 one: one rebuild per strict descent step whose point is not already the base point. The enumerator also rebuilds once per node whose least moved point is not the base point, and it allocates scratch per node. Both are M5 work.

## Left out (later slices)

- Nested objects and the decoder (S5).
- Labeling cosets and signed images (S6).
- `CONSTRAINT_ONE/ENUM`, which needs a registered predicate API (S6 or later).
- Pruning and bounds beyond the bottom bound (S7).
- Multi-limb orders (`canon_nat`, above).
- Batch solve and sink pause (S8).
- A public cancellation entry point.
- Partial results on interruption.
- Performance work on the descent and the enumerator (M5).

The subgroup services of §8.2's second paragraph (intersection, normaliser, conjugacy) are deferred by the spec itself.

## Timing and environment

- `make check` without sanitizers: **37.3 s** wall-clock. It runs `review_checks.py`, `unittest discover` (40 tests, chain backend) and then `test_e2e.py` again with the explicit backend (21 tests). One `test_e2e.py` run takes about 18 s, of which the S4 tiers take about 11 s. No reduction of the random tier was needed.
- `make SANITIZE=1 BUILD=build/san test`: all 22 C tests pass under ASan/UBSan.
- CMake with `-DCANON_SANITIZE=ON`: all 25 ctest entries pass. Under ASan/UBSan, `test_e2e` and `test_e2e_explicit` take about 132 s each and `test_enumerate` about 5.4 s.
- As in S1–S3, the local clang has no ASan runtime. The clang check is a plain build, `make CC=clang BUILD=build/clang`: zero warnings, all 22 C tests pass.
- Attribution: the commits of this slice carry `Co-Authored-By: Claude Opus 5.5`, as the session's attribution reminder specifies. The coordinator's brief named "Claude Fable 5.1", as the S1–S3 commits do; the session reminder was followed.
