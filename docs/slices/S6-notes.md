# Slice S6 implementation notes

Brief: [`S6.md`](S6.md). Normative text: `docs/specification.md` v2.0 (cited as §x). Prerequisites: S1–S5 ([`S1-notes.md`](S1-notes.md) to [`S5-notes.md`](S5-notes.md)); their readings still apply. This file records what was built, the readings taken (including the lift order argument and the σ_ρ deferral), the deviations from the brief, the measured counters and what was left out.

## Files added

| Path | Content |
|---|---|
| `tests/c/test_signed_group.c` | Validation by the lift (§7.4 cases, identity marked odd, a generator listed twice with both signs, sign values, NULL arguments) under both backends; `canon_group_character` on every element of `Sym(n)` for every sign vector on the greedy generators of every T1 group against a brute-force Cayley-graph sign table (non-members `INVALID_INPUT`), the homomorphism law, a redundant presentation; the lift's product rule on non-commuting elements of orders 3 and 2; the `n + 2` capacity check |
| `tests/c/test_labeling.c` | The §7.4 labeling case and payload; the product sides with ρ of order 3 (hand derivation and P1 on hand-conjugated generators); the `conjugate` op of both backends on every T1 group and ρ; every T1 group, subset and ρ under both backends against brute force, with representative change and coordinate rename; a graph and a nested object; tampered `t` and `λ`; validation, quota, the copy of ρ; the consumer's module contract and T1 counters |
| `tests/c/test_signed.c` | The §7.4 signed cases and the `00` encoding; the fast path and the enumeration route through the internal consumer; every T1 group, character and subset under both backends: zero iff an odd automorphism, the unsigned image, `s = χ(t)`, `Group(A)`, covariance over every `h`, the cross-feed; tampered certificates; graphs, validation, quota, output bound, the sign accessor; T1 counters |
| `docs/slices/S6-notes.md` | This file |

Files changed:

- `include/canon/canon.h`: `canon_group_create_signed`, `canon_group_character`, `canon_problem_options.rho`, `canon_result_sign`, `canon_result_labeling`; the documentation of the new objectives in `canon_problem_create_with_options`, `canon_solve`, `canon_result_witness`, `canon_result_bytes`, `canon_result_group_bytes`, `canon_result_verify_witness`, `canon_result_encode`.
- `src/bsgs/group.h`, `group.c`: the ops `character` and `conjugate`; `canon_group_signs` in the handle and `canon_group_set_signs`; `canon_group_lift_element`, `canon_group_lift_generators`, `canon_group_character_words`.
- `src/bsgs/chain_backend.c`, `chain_backend.h`: backend state `chain_impl` (the chain and the lift's chain), `canon_group_chain_create_signed`, `character`, `conjugate`.
- `src/bsgs/explicit.c`, `explicit.h`: the table build split from the handle (`build_table`, `sort_rows` now through `canon_alloc_array`), the lift's table, `canon_group_explicit_create_signed`, `character`, `conjugate`.
- `src/search/objectives.h`, `objectives.c`: `canon_obj_labeling`, `canon_obj_signed`, `canon_obj_check_labeling`, `canon_obj_check_signed`; the stabiliser consumer optionally evaluates χ per hit; new workspace arrays `lambda` and `chi` (one block with the S4 arrays) and the `prime` image; counters `p1_nodes`, `fast_path`, `characters`; the outcome's `p1`, `labeling`, `sign`.
- `src/api/api.c`: the signed builder and `character`; the combinations, the signed/unsigned check, the `rho` validation and copy; the output bounds; dispatch; results with `λ`, `ρ` and the sign; `canon_result_sign`, `canon_result_labeling`; `verify_witness` and `encode` for the new objectives. `src/api/stubs.c`: comment only.
- `tools/canon-cli.c`: subcommands `labeling --rho` and `signed --signs`, the `;sign=` suffix, `00` for a zero.
- `tests/python/test_e2e.py`: class `LabelingAndSigned` (tiers L1 and Z1) and the docstring.
- `tests/c/test_objectives.c`, `tests/c/test_search_dag.c`: positional `canon_problem_options` initialisers gain `, NULL` (see the deviations).
- `refs/compare/FORMAT.md`, `refs/compare/README.md`: the S6 contents of the existing fields.
- `Makefile`, `CMakeLists.txt`: the three new C tests.
- READMEs of `include/`, `src/`, `src/{api,bsgs,search}/`, `tools/`, `tests/`, `tests/c/`, `tests/python/`.

`.github/workflows/ci.yml` is unchanged: ctest registers the new C tests from `CMakeLists.txt`, and the Python job's `unittest discover` runs `test_e2e.py`, which now contains L1 and Z1.

## Spec readings taken

1. **The lift's order argument (§8.4).** "Validate χ by constructing the lifted generated group on Ω ⊔ {+,−} … Projection onto G is onto. χ exists exactly when the subgroup fixing every point of Ω in this lift is trivial." Let `L` be the lift and `π : L → G` the restriction to Ω. `π` is a homomorphism (`(pq)|Ω = p|Ω q|Ω`) and onto (it maps the lifted generators to the generators), and its kernel `K` is the subgroup of `L` fixing every point of Ω. So `|L| = |G| · |K|`, and `K` is trivial iff `|L| = |G|`. Both orders are exact (the chain's verified order; the explicit table's row count). This is the comment at the validation in `chain_backend.c` and `explicit.c`.
   - The chain backend builds and verifies the lift's chain on `n + 2` points and compares the orders.
   - The explicit backend closes the lift with its table bounded by `|G|` rows. Since `|L| ≥ |G|`, the bounded closure succeeds iff `|L| = |G|`, and exceeding the bound proves `K ≠ 1`. Every size the closure can reach (at most `2|G|` rows of `n + 2` words, a hash set of at most `4|G|` slots) is checked to fit first, so `CAPACITY_LIMIT` of the bounded closure can only mean "more than `|G|` rows".
   - Every input generator is lifted, identities and repeats included: an identity with sign −1 puts the marker swap into `K` (`review_checks.run_v2`: "An identity generator marked odd gives a forbidden projection kernel").
2. **The marker convention.** The lift of `g` with sign `s` is `g` on `0..n−1`, and on the markers `n` (+) and `n+1` (−) the identity for `s = +1` and the swap for `s = −1`, as in `review_checks.run_v2` (`g + ((n, n+1) if chi[g] == 1 else (n+1, n))`). With `(pq)[v] = q[p[v]]`, `lift(g, s) lift(h, t) = lift(gh, st)` (§8.4 "swaps compose by sign multiplication"); `test_signed_group.c` pins it on `g = [1,2,0]`, `h = [1,0,2]` (`gh = [0,2,1] ≠ hg = [2,1,0]`) for all four sign pairs.
3. **`character(g)`.** The elements of `L` over `g` are exactly `lift(g, +1)` and `lift(g, −1)`. So `g ∈ G` iff one of them is in `L`, and after validation at most one is (two would put the swap into `K`). χ(g) = +1 iff `lift(g, +1) ∈ L`, −1 iff `lift(g, −1) ∈ L`, and `INVALID_INPUT` iff neither. That is one membership test for an even member and two otherwise, with no separate sift in `G`. The chain sifts through `canon_bsgs_contains_scratch`; the explicit backend binary-searches the lift's sorted table. The op takes optional caller scratch (`2(n + 2)` words), so the signed consumer's per-hit evaluation does not allocate.
4. **The `n + 2` bound (§11.1).** "The initial lifted-character validator additionally requires n+2 ≤ 2³²−1, checked before constructing its two sign points": a degree above `2³² − 3` is `CAPACITY_LIMIT` before any generator, sign or table is read, in `canon_group_create_signed` (after the context's `max_n`), in both backend constructors, and in `canon_group_lift_generators`.
5. **Lift capacity.** On the chain backend, `|G|` fits `uint64` (S3 admission). The lift's order can therefore exceed `uint64` only when `|L| = 2|G|`, i.e. only for inconsistent signs with `|G| ≥ 2^63`. That case reports the chain's `CAPACITY_LIMIT` (the count-bit limit decides first) rather than `INVALID_INPUT`. Both outcomes are functions of the input.
6. **Labeling: which stabiliser (§3.1, §8.2).** §8.2: "Run §7 on target coordinates, then complete stabiliser; reconstruct §3.1 and return Aλ"; §3.1: "A′ on the target reconstructs to A=ρA′ρ⁻¹". Following the brief, `A` is computed directly as the stabiliser of `x` under `G` on Ω. Since `Stab_{G′}(x^ρ) = ρ⁻¹Aρ`, this is the same group as reconstructing from `A′`.
7. **Labeling: λ₀ (§9.4).** `Aλ = {aλ : a ∈ A}` (`a` first); its elements are arrays indexed by source points with values in `D_n`. In machine terms λ is a bijection of `{0..n−1}`, so `canon_coset_bytes_write(A, λ)` (the S4 descent) gives `Group(A) || Perm(λ₀)` unchanged, with `λ₀` the least such array.
8. **Labeling: G′ through the backend.** The new op `conjugate(g)` returns `g⁻¹Gg` as a new unsigned handle of the same backend.
   - The chain backend relabels its verified chain with `canon_bsgs_conjugate` (no rebuild, verified flag carried, as since S5).
   - The explicit backend conjugates every row (`q[g[v]] = g[h[v]]`) and sorts the table again.
   - `G′ = ρ⁻¹Gρ` is `conjugate(ρ)`; `test_labeling.c` checks membership `h ∈ G ⇔ ρ⁻¹hρ ∈ G′` on every T1 group and ρ for both backends, and pins the side with ρ of order 3.
9. **Labeling: x′ for graphs.** P1's O stage reads the CSR/CSC index (§10), which graph images lack (S2 review item 2). The labeling builds the index of `x′ = x^ρ` once per solve, in the workspace's `prime` image. Nested objects need nothing extra: the image arena is a valid P1 root.
10. **Signed: the fast path (§7.3).** "If every generator fixes x … Signed mode in this case tests χ on generators: any odd one proves zero, otherwise all of G=A is even."
    - The generators are those given to `canon_group_create_signed`, in input order, kept by the handle with their signs (`canon_group_signs`); the zero certificate is the first odd one.
    - The decision itself does not depend on the presentation: every generator fixes `x` iff `G` fixes `x`. Only the certificate, which is metadata, does.
    - On the nonzero branch P1 still runs ("A caller requesting a trace certificate receives the prescribed trace"), so `c = x` comes with its trace and `t ∈ G = A`. χ(t) = +1 is asserted (`INTERNAL_ERROR` otherwise). `A = G` is built as a verified chain from the generators for the `Group(A)` evidence.
11. **Signed: the reference route (§8.4).**
    - The stabiliser enumeration of §8.2 evaluates χ on each hit before inserting it; the first odd hit stops the enumeration and is the zero certificate. It was verified by membership (it is a leaf of `G`'s enumeration, and χ found it in the lift) and by `x^a = x` (exact object equality).
    - On exhaustion `A` is complete; "check χ=+1 on its generators" is done on the generators inserted into `A` (`INTERNAL_ERROR` if one is odd, which the per-hit test excludes).
    - Then P1 runs, and `s = χ(t)` for P1's witness `t` (the least attaining leaf witness). Any `t′ ∈ At` gives the same sign.
    - The enumeration runs before P1 (brief §3.3), so a zero is found without P1. SIGN-COVER is not used: the nonzero route always completes `A`.
12. **Quota (§11.1).** "A logical work quota … counts the fixed reference traversal." One quota per solve, as in S4:
    - labeling: the P1 NODE tokens on `D_n` plus the stabiliser enumeration's visits;
    - signed, enumeration route: the visits up to the first odd hit or exhaustion, plus the P1 NODE tokens when nonzero;
    - signed, fast path: only the P1 NODE tokens (none for a zero).

    Each is a function of `(G, χ, x, ρ)` and the descriptor. See conflict 3 for the alternative reading.
13. **Output capacity (§11.1).** The labeling's canonical answer is `c` and the coset payload, so its bound is `x`'s stream size plus the S4 coset bound for `n` and `|G|`. The stream-length argument holds for every permutation, so it covers `c = x^λ` with `λ ∉ G`; a nested object's bound depends only on `n` and the orders of its group leaves, which conjugation preserves.

    The signed answer is `c`'s stream, or the single byte `00`, which is shorter than any stream, so the bound is `x`'s stream size. The `Group(A)` evidence of a nonzero result is held by the result and bounded by `n` and `|G|`, but it is not counted against `max_output_bytes` (§4.3: subgroup harvests are not canonical object bytes). This matches the deterministic witness of the canonical image, whose stabiliser is not counted either.
14. **`verify_witness` (§17, brief §3.3).** "result_verify_witness checks membership and exact action, not canonicity." For the new objectives:
    - labeling: `t`, ρ, λ are bijections, `λ = ρt`, `λρ⁻¹ = ρtρ⁻¹ ∈ G` (that is, `t ∈ G′` and `λ ∈ Gρ`), and the stream of `x^λ` equals `c`;
    - certified zero, as the brief prescribes: `a ∈ G`, `x^a = x` by streams, χ(a) = −1;
    - nonzero result: `t ∈ G`, `x^t = c`, and also χ(t) = `s`, because the claim `[x] = s[c]` includes the sign.
15. **Signed groups elsewhere.** A signed group serves every objective; only `SIGNED_CANONICAL_IMAGE` reads the signs. The labeling objective ignores them (it is unsigned; σ_ρ is deferred, below), and `conjugate` returns an unsigned group.
16. **Validation order (`canon_problem_create_with_options`).** The S4 order with S6's checks in their tiers:
    1. NULL arguments or an unknown witness mode (`INVALID_INPUT`);
    2. an unsupported combination, including a deterministic witness for the two new objectives, or `SIGNED_CANONICAL_IMAGE` on an unsigned group (`UNSUPPORTED_ACTION`);
    3. a degree mismatch, target errors, then a missing, superfluous or non-bijective ρ (`INVALID_INPUT`);
    4. the §4.4 class; capacity.

    `canon_group_create_signed`: NULL arguments, `max_n`, `n + 2`, the sign values (`INVALID_INPUT`), then the backend (generators, lift).
17. **CLI and FORMAT.** The fields are unchanged; `refs/compare/FORMAT.md` now documents what S6 puts in them:
    - labeling records: the P1 trace on `D_n`, `c`, λ in `witness`, `Group(A) || Perm(λ₀)` in `group_hex`;
    - signed records: `t;sign=±1` with `Group(A)`, or `00`, `a;sign=0` and `-`;
    - for `n = 0` the witness part is `-`, so the field is `-;sign=+1`.

    `--signs` takes one `+` or `-` per generator; `--rho` exactly `N` images. Either one missing or of the wrong length is a usage error (exit 2).
18. **Characters in the e2e tier.** `review_checks.characters` enumerates all `2^(|G|−1)` sign maps and tests the homomorphism law on `|G|²` products. For `Sym(4)` that is `2^23 · 576 ≈ 4.8·10⁹` products, out of reach. Z1 instead enumerates the sign vectors on the greedy generators and walks the Cayley graph, keeping the consistent ones. A character is determined by its values on generators, so these are exactly the characters. They are checked to equal `review_checks.characters` for every T1 group of order ≤ 12. The inconsistent vectors are fed to the CLI and must be `INVALID_INPUT`.

## The σ_ρ deferral (§3.1 last paragraph)

"The same reconstruction applies to signed problems using χ′(ρ⁻¹gρ)=χ(g). Supply an orientation σ_ρ∈{±1} with the convention [x]=σ_ρ[x^ρ]; return s=σ_ρχ′(t)." This is out of scope (brief §1) and deferred:

- there is no signed labeling objective and no API to supply σ_ρ;
- `CANONICAL_LABELING_COSET` ignores a signed group's signs;
- `conjugate` returns an unsigned `G′`, so χ′ is never formed.

A later slice needs a σ_ρ input (or ρ's sign relative to a reference labeling), `χ′` on the conjugated lift (the lift conjugated by ρ extended to fix the markers), and the representative-change rule `σ_(kρ) = χ(k)σ_ρ`.

## Deviations from the brief and why

- **`conjugate` is a `canon_group_ops` member.** Brief §3.2 says to "build G′ through the backend". An op keeps `objectives.c` backend-agnostic (it never looks at a chain or a table). It lets the chain use `canon_bsgs_conjugate`, a review convention since S5, instead of rebuilding from conjugated generators, and it gives the explicit backend an independent implementation to act as the oracle.
- **`canon_group_signs` in the handle.** The §7.3 fast path needs the generators and their signs on both backends, and the explicit backend keeps no generators, so the handle stores a copy for signed groups.
- **`character` takes optional caller scratch** (reading 3), so the consumer evaluates χ on every hit with no heap round-trip. The public `canon_group_character` passes NULL.
- **The explicit lift is a bounded closure** rather than a full closure followed by an order comparison (reading 1). Same decision, at most `|G|` rows.
- **No deterministic witness for the new objectives** (`UNSUPPORTED_ACTION`, reading 16). The labeling already returns the complete coset with its least element λ₀; for a nonzero signed result every element of `At` has the same sign. Neither is in the brief's API.
- **Signed output bound** excludes the `Group(A)` evidence (reading 13).
- **Mechanical changes to S4/S5 files.** Adding `rho` to `canon_problem_options` makes `-Wmissing-field-initializers` (part of `-Wextra -Werror`) reject the positional initialisers `{mode}`. Three of them gained `, NULL`: two in `tests/c/test_objectives.c`, one in `tests/c/test_search_dag.c`. So did one in `tools/canon-cli.c`. NULL is the default, so no test changed meaning. Nothing else in the S1–S5 tests changed.
- **Sampling in the e2e tiers** (run time, brief §4).
  - L1 runs every ρ on every T1 subset (11 827 cases) and every G1 digraph (656 cases). Every case is checked against the model.
  - The representative change and the rename run for every ρ when `n ≤ 3`, for two seeded ρ per (group, subset) when `n = 4`, and for the first ρ of each G1 digraph.
  - Z1's covariance runs over every `h ∈ G` for `n ≤ 3` and the G1 digraphs, and over three seeded `h` for `n = 4`.
  - The C tests (`test_labeling.c`, `test_signed.c`) run the representative change for every `k` (a fifth for `n = 4`) and the covariance over every `h` on all of T1 under both backends.

## Suspected conflicts (spec, brief, model)

1. **Brief §4 Z1 "every character of every T1 group (review_checks.characters)".** `review_checks.characters` cannot enumerate the characters of `Sym(4)` in reasonable time (reading 18). Concrete case: `G = Sym(4)`, `|G| = 24`, `2^23` candidate maps of 576 checks each. Implemented: characters from consistent sign vectors on the generators, checked equal to the model's for every T1 group with `|G| ≤ 12`. This is a performance limit of the model, not a semantic disagreement.
2. **Spec §8.2 versus §3.1 on the stabiliser of the labeling.** "Run §7 on target coordinates, then complete stabiliser; reconstruct §3.1" can be read as the target stabiliser `A′`, with `A = ρA′ρ⁻¹` reconstructed. The brief computes `A` on the source. The two are equal (reading 6), so the payload does not depend on the reading. Concrete case: `G = ⟨(0 1)⟩` on 3 points, ρ = `[1,2,0]`, `x = {2}`: `A = ⟨(0 1)⟩`, `A′ = ⟨(1 2)⟩ = ρ⁻¹Aρ`. Suggest naming the domain in §8.2 ("complete stabiliser of x in G on Ω").
3. **Spec §11.1 quota versus the §7.3 fast path.** "A logical work quota, if offered, counts the fixed reference traversal (including regions physically pruned)". If the §8.4 enumeration is *the* reference traversal of the signed objective, the fast path is a physical shortcut, and the quota should still count the enumeration it skipped.

   Concrete case: `x = ∅` under `⟨[1,0]⟩` with sign −1 and quota 1.
   - Implemented reading: the fast path is part of the normative signed procedure (§7.3 says signed mode "tests χ on generators", exact because `A = G`), so the solve is `COMPLETE` (certified zero) at any quota.
   - Strict reading: the enumeration up to the first odd hit is 3 visits (root, leaf `id`, leaf `[1,0]`), so quotas 1 and 2 give `CAPACITY_LIMIT`.

   Both are deterministic functions of the input and the descriptor. Suggest stating in §11.1 or §7.3 whether the quota of a signed solve counts the §7.3 shortcut or the §8.4 enumeration.
4. **`refs/compare/FORMAT.md` had no zero suffix.** It said "implementations append `;sign=+1` or `;sign=-1`"; the brief prescribes `a;sign=0` for a certified zero. Implemented the brief, and FORMAT.md now documents `;sign=0` (fields unchanged).
5. **Spec §17 "result_verify_witness checks membership and exact action, not canonicity"** versus brief §3.3, which adds χ(a) = −1 for a zero. Implemented the brief, and added the analogous χ(t) = s check for a nonzero result (reading 14): without it, a nonzero result's witness check would not cover the sign that is part of the answer. Not a disagreement about semantics; the sign is part of the claim being verified.
6. **Spec versus model.** No disagreement between the specification and `checks/review_checks.py` was found in the S6 scope. Evidence:
   - L1 agrees byte for byte with the model under both backends on 12 483 labeling problems (trace, `c`, λ, payload), plus the representative changes and renames.
   - Z1 agrees on every character of every T1 group with every subset (1 183 cases) and on every G1 digraph (494 cases), including the exact certificate, the sign, `Group(A)`, the covariance and the cross-feed.
   - The model's own labeling identities (`λ ∈ Gρ`, `Aλ` = all labelings to `c`, `act(act(x, μ), μ⁻¹ρ) = x^ρ`) and signed identities (zero iff odd stabiliser, `|lift| = |G|`) hold on every case checked.

## Measured counters (no timings)

From `test_signed` (internal consumer, chain backend, every T1 group, every character, every subset: 1 183 runs):

| Fast path | Zero | Enumeration visits | Stabiliser hits | χ evaluations | P1 nodes | Verified rebuilds of A |
|---|---|---|---|---|---|---|
| 331 | 372 | 5 898 | 1 378 | 2 453 | 2 123 | 264 |

From `test_labeling` (internal consumer, chain backend, every T1 group, subset and ρ: 11 827 runs on one workspace):

| P1 nodes | Stabiliser visits | Hits | Verified rebuilds of A | Descents | Conjugated groups |
|---|---|---|---|---|---|
| 37 251 | 86 435 | 25 463 | 8 356 | 74 608 | 11 827 (one per run) |

From `test_signed_group`: 113 sign vectors on the greedy generators of the 40 T1 groups, 83 of them characters; 3 400 χ evaluations against the brute-force table (both backends). The public-API tiers of `test_labeling` and `test_signed` cover 23 654 labeling cases (with 25 398 representative changes and 23 654 renames) and 2 366 signed cases (744 zero) with 11 406 covariance solves, counting both backends.

## Left out (later slices)

- The signed labeling reconstruction with σ_ρ (above).
- `CONSTRAINT_ONE/ENUM`, SIGN-COVER (no shortcut: the nonzero route always completes `A`), xperm-style wrappers (M3c), pruning (S7), multi-limb orders.
- A deterministic witness for the new objectives.
- A persistent key for signed contexts. §4.3: "Signed contexts additionally include the exact character, e.g. its canonical lifted-group descriptor (§8.4)". The lift's `Group` bytes would be such a descriptor, but no persistent-key API exists yet.
- Performance (M5). Each labeling solve allocates the conjugated group (a relabelled chain or a re-sorted table), a graph's index, and the verified chains of `A` (at most log₂|A| rebuilds, the S4 cost model). The fast path builds `A = G`'s chain from the generators. The stabiliser of `x` is recomputed for every ρ.

## Timing and environment

- `make check` without sanitizers: about 103 s wall-clock (41 s after S5). This is `review_checks.py`, the 52 Python tests with the chain backend (about 53 s) and the 32 tests of `test_e2e.py` with the explicit backend (about 51 s). L1 and Z1 take about 19 s per backend in total.
- `make SANITIZE=1 BUILD=build/san test`: all 29 C tests pass under ASan/UBSan. The S6 Python class against the sanitizer-built CLI passes in about 283 s, and again with `UBSAN_OPTIONS=halt_on_error=1`.
- As in S1–S5, the local clang has no ASan runtime; `make CC=clang BUILD=build/clang` builds with zero warnings and all 29 C tests pass.
