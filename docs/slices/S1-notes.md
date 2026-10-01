# Slice S1 implementation notes

Brief: [`S1.md`](S1.md). Normative text: `docs/specification.md` v2.0 (cited as §x). This file records what was built, the readings taken of sentences that admit more than one implementation, the deviations from the brief, and what was left out.

## Files added

| Path | Content |
|---|---|
| `src/perm/perm.c` | §3 dense primitives: compose, inverse, apply_tuple, lex_compare, is_identity, validate |
| `src/encoding/wire.h`, `wire.c` | growable byte buffer; `U16`, `U32`, `B` writers (§4.1); unsigned byte order with proper prefix smaller (§4.3, §7.2) |
| `src/encoding/subset_stream.h`, `subset_stream.c` | CDAG-2 stream of a top-level subset (§4.1, §4.2 specialisation) and its exact length |
| `src/object/subset.h`, `subset.c` | subset object: bitset plus sorted members, dedup (§4.2), range check (§4.1), membership key (§7.1), action (§2.1) |
| `src/bsgs/group.h`, `group.c` | group interface `canon_group_ops` (order, contains, tuple_min with §7.1-ordered `G_M` orbit ids), the `canon_group` handle struct and `canon_group_alloc` (sole constructor; sets the reference count) |
| `src/bsgs/explicit.h`, `explicit.c` | explicit-enumeration backend: BFS closure, capacity checked before each growth, sorted element table, union-find orbits |
| `src/partition/partition.h`, `partition.c` | ordered partition (§10 layout), §7.1 split and individualisation, snapshot rollback (§11.2) |
| `src/refine/p1.h`, `p1.c` | §7.1 root partition for a subset and the node loop (empty O stage, G stage), §7.2 tokens |
| `src/search/p1_tree.h`, `p1_tree.c` | unpruned §7.1 tree, §7.2 leaf map and `(trace, bytes)` minimum, least attaining leaf witness, §11.1 NODE quota |
| `src/api/api.c` | context, capacity descriptor, retain/release, group/subset/problem/workspace builders, `canon_solve`, result accessors, `canon_result_encode` |
| `src/arena/checked.h` | overflow-checked `size_t` multiply/add (§11.1) |
| `src/arena/refcount.h` | atomic reference counts for shareable handles (§17) |
| `src/util/sort.h`, `sort.c`, `README.md` | the one stable merge sort used by the group backend and the partition |
| `tools/canon-cli.c` | `canon-cli p1-subset` emitting `refs/compare/FORMAT.md` records |
| `tests/c/check.h` | shared assertion helper and deterministic PRNG |
| `tests/c/test_perm.c`, `test_sort.c`, `test_wire.c`, `test_group_explicit.c`, `test_partition.c`, `test_search_subset.c` | C unit and end-to-end tests |
| `tests/python/test_e2e.py` | CLI against `checks/review_checks.py` (T1 tier) and `golden.json` |
| `docs/slices/S1-notes.md` | this file |

Files changed: `include/canon/canon.h` (S1 API, `canon_problem_create` gains `capacity`), `include/README.md`, `src/perm/perm.h` (new declarations; `canon_perm_validate_scratch` plus the allocating `canon_perm_validate`, which returns `int`), `src/api/stubs.c` (replaced stubs removed), `tests/c/test_version.c` (see deviations), `tests/c/test_header_abi.c` (comment only), `Makefile`, `CMakeLists.txt`, `.github/workflows/ci.yml` (the Python job builds `canon-cli` before running the unit tests, because `test_e2e.py` needs it), and the READMEs of `src/`, `src/{api,arena,bsgs,encoding,object,partition,perm,refine,search}/` (plus the new `src/util/README.md`), `tools/`, `tests/c/`, `tests/python/`.

## Spec readings taken

1. **Witness (§3 versus §7.4 preamble).** §3: "A deterministic witness is optional metadata: minimise the image array among all solutions sending x to the selected c". §7.4: "Where identity attains the minimum key it is the least witness; elsewhere minimise among the witnesses that actually attain that key." S1 reports the least `t_L` over the leaves attaining the least `(trace, bytes)` key, as the brief and `review_checks.p1` do. For the **unpruned** tree this is exactly the §3 deterministic witness: by §7.2 equivariance (`t_(L^h) = h⁻¹ t_L`, with `h = a ∈ Aut_G(x)` mapping the tree of `x` to itself), the leaves attaining the key carry the whole coset `Aut_G(x)·t`, and every attaining leaf sends `x` to `c` (injective encoding), so the attaining leaf witnesses are exactly `{g : x^g = c}`. `test_e2e.py` asserts this over all 539 T1 cases. The brief's warning that the two differ applies once pruning (S7) removes leaves; the header comment on `canon_result_witness` says this.
2. **G-stage orbit order (§7.1).** "sort each orbit's target labels increasingly, then sort the orbit lists lexicographically": the orbits are disjoint, so the lexicographic order of the sorted lists is the order of their least points. The backend ranks union-find roots (each root the least point of its class). The transporter `u` is the least element with `F^u = M` ("choose any u"; §7.1 proves the pulled-back cells do not depend on the choice).
3. **Leaf map (§7.2).** `L` is `lab` when every cell is a singleton; `t_L` is found by scanning the sorted table for the least `L^g` (unique because `L` is a full list).
4. **Logical work quota (§11.1).** "A logical work quota, if offered, counts the fixed reference traversal (including regions physically pruned)". `max_search_nodes = K` admits a traversal with at most `K` NODE tokens; the search is abandoned when it is about to emit the `(K+1)`-th NODE, so `CAPACITY_LIMIT` occurs iff the unpruned tree has more than `K` nodes, independent of visitation order. The result then carries only the status.
5. **Output capacity (§11.1).** "For data-dependent output size, use a count-only canonical traversal/encoder or a conservative input-derived bound". For a subset the canonical stream length is exactly `24 + 9|x|` (the image has `|x|` members), so the bound is exact and is checked in `canon_problem_create`.
6. **Arithmetic overflow versus allocation failure (§4.1, §11.1, §17).** A size or length that does not fit (`U32` field, `size_t` product) is `CANON_CAPACITY_LIMIT` ("overflow is a capacity error"); a failed `malloc`/`realloc` is `CANON_RESOURCE_LIMIT` and leaves the previous state (§17).
7. **Sink (§17).** "Sink failure may leave `image_canonical=true` and `encoding_complete=false`". Results are immutable in S1 (brief: "with the result unchanged"), so a sink failure returns `CANON_OUTPUT_ERROR` and leaves `encoding_complete` true: the flag describes the bytes the result holds, not their delivery, and `canon_result_encode` may be called again. The return value `CANON_COMPLETE` is the commit marker of the "final commit marker/length" sentence. Partial acceptance (`*accepted` in `1..length`) re-offers the rest; zero progress, `*accepted > length`, pause (`1`, deferred to S8) and any other nonzero return are failures, so no byte is skipped or duplicated.
8. **Capacity descriptor zeros.** A zero field of the problem descriptor selects the context default (brief); a zero field of the context `defaults` (or `defaults == NULL`) selects the built-in default for that field.
9. **Where limits apply.** `canon_group_create` enforces the context's `max_n` and `max_group_order` (the closure itself needs a bound); `canon_object_create_subset` enforces the context's `max_n`; `canon_problem_create` re-checks degree, group order and output size against the resolved problem descriptor. A problem descriptor therefore cannot admit more than the builders already admitted.
10. **Status precedence.** Builders check the admitted degree first, then the data (invalid generator or atom). `canon_problem_create` checks NULL arguments, then unsupported IDs (§3.2/§4.1 "unsupported, never reinterpreted"), then degree mismatch, then capacity. The spec does not fix an order.
11. **n = 0.** The witness is produced (`canon_result_witness` returns a non-NULL pointer with `*degree = 0`); the CLI writes `-` per `FORMAT.md`. On degree 0 every generator is the empty permutation and is not read.
12. **Const handles.** `canon_problem_create` takes `const canon_group *` and `const canon_object *` but retains them; only the reference count changes, never the immutable content. The count is atomic and is reached through a pointer member, so no const-cast is involved (see Review fixes, item 2).

## Deviations from the brief and why

- **Where `INVALID_INPUT` and `UNSUPPORTED_ACTION` surface.** The brief lists them under "`canon_solve` returns"; it also says `canon_problem_create` validates objective, profile, encoding, order and degrees. Since an invalid problem cannot be built, these statuses come from `canon_problem_create` (no problem is returned); the policy lives only there. `canon_solve` returns `INVALID_INPUT` for NULL arguments.
- **`canon_perm_validate` returns `int`** (1 valid, 0 invalid, -1 scratch allocation failure) rather than `bool`, so that an allocation failure is not reported as invalid input; `canon_perm_validate_scratch` returns `bool` and does not allocate.
- **`canon_group_ops` has a `destroy(impl)` member** and `struct canon_group` carries the reference count, `impl` and its own allocation pointer; the interface needs a way to free the backend behind an opaque handle.
- **`tests/c/test_version.c` updated.** It asserted that `canon_solve`, `canon_group_create`, `canon_problem_create`, `canon_workspace_create` and `canon_result_encode` return `CANON_UNSUPPORTED_ACTION` for NULL arguments; they now return `CANON_INVALID_INPUT`, and `canon_problem_create` has the new `capacity` parameter. The checks on the remaining stubs are unchanged.
- **`src/arena/checked.h`, `src/arena/refcount.h` and `src/util/`** are new. `checked.h` holds shared overflow-checked size helpers (a plain `x > SIZE_MAX / k` test is a compile error under `-Wtype-limits -Werror` on 64-bit targets when `x` is a widened `uint32_t`).
- **CI**: `.github/workflows/ci.yml` gains one step (`make build/make/canon-cli`) in the Python job and sets `CANON_REQUIRE_CLI=1` on the unit-test step; the shape of the workflow is unchanged.
- **Result for non-complete solves.** On `CAPACITY_LIMIT` (and `RESOURCE_LIMIT`, when it can be allocated) `canon_solve` returns a status-only result, as the brief requires for `CAPACITY_LIMIT`.

## Spec/model agreement

No disagreement between the specification and `checks/review_checks.py` was found within the S1 scope. Evidence: all 539 T1 cases (every subgroup of `Sym(n)`, `n ≤ 4`, every subset), each with the full element list and the greedy generating sequence as generators, give identical trace, bytes and witness in C and in the model; the four §7.4 subset cases match `golden.json`; and the model's witness equals the §3 deterministic witness on every T1 case (reading 1). The model's `normalized_orbits` and the C backend take the same reading of the G-stage ordering (reading 2).

## Left out (later slices)

Graphs and the O-stage counts (S2); stabiliser chains, sparse permutations, multi-limb orders (S3); `canon_result_verify_witness` and every objective other than `CANONICAL_IMAGE` (S4, S6); `canon_object_create` from a stream, the general §4.2 normaliser and the decoder (S5); pruning, certificates and the `TRUSTED_ENGINE`/`CHECKED_CERTIFICATE` evidence mode in the result (S7); trail rollback, memory ledger, `solve_batch`, sink pause/resume (S8); registries and checkpoints (M6). Single-owner enforcement of workspaces is not checked (documented in `canon.h`).

Known costs of the S1 backend: the element table is `|G|·n·4` bytes (up to 1 GiB at the default limits `n = 4096`, `|G| = 2^16`; failure is `RESOURCE_LIMIT`), and each `tuple_min` is O(|G|·n). Snapshots cost O(n) words per depth; depth is at most log₂|G| because each individualisation below a G stage strictly shrinks the stabiliser.

## Environment notes

- The local clang has no AddressSanitizer runtime (`libclang_rt.asan-x86_64.a` is missing), so `cmake -DCANON_SANITIZE=ON` with `CC=clang` cannot link here; it is a toolchain gap, not a code issue (the CI clang+sanitizer job was not run from here; the ubuntu-latest clang normally ships the runtime). Locally the clang build was exercised with `-fsanitize=undefined -fsanitize-trap=all` (all C tests and `test_e2e.py` pass), and gcc with ASan+UBSan.
- `README.md`, `HANDOFF.md` and `CLAUDE.md` described the pre-S1 state (only `canon_version()` and stubs); the coordinator is updating them, and this slice does not touch them.

## Review fixes

The S1 review found no core-algorithm bug and nine items, all fixed on this branch. Every §6 command was then run again.

1. **CLI ids.** `tools/canon-cli.c` rejects an `--id` that is empty, begins with `#` (a `refs/compare/FORMAT.md` comment line) or contains TAB, CR or LF, with usage exit 2. `test_e2e.py` covers all three.
2. **Atomic, cast-free reference counts.** New `src/arena/refcount.h`: `canon_refcount` holds an `_Atomic size_t`, with `canon_ref_init`, `canon_ref_retain` (relaxed) and `canon_ref_release` (acq_rel; true on the last reference). Shareable handles (`canon_group`, `canon_object`) store the count in their own allocation and reach it through a pointer member. Through a const handle the pointer is const but its target is not, so `canon_problem_create` keeps its const signature and retains with no const-cast. On the last release the handle is freed through its stored allocation pointer (`block`). Problems, workspaces and results use a plain atomic member. `canon.h` documents that handles may be shared between threads for retain/release and read-only use, that workspaces have one owner, and that the count is bookkeeping rather than logical state.
3. **Skip without a CLI.** Without a built CLI, `test_e2e.py` calls `skipTest` with a clear message. `CANON_REQUIRE_CLI=1` turns the skip into a failure. It is set by the Makefile `check` target, by CMake's `test_e2e` environment and by the CI unit-test step. A standalone discover on a fresh checkout reports `OK (skipped=5)`.
4. **Snapshot accessor.** `canon_partition_snapshot_lab(snap, n)` in `partition.h`. The layout is private to `partition.c`, and the search uses the accessor.
5. **One stable merge sort.** `src/util/sort.{h,c}` provides `canon_stable_sort(base, count, size, tmp, cmp, ctx)`. `explicit.c` sorts row indices with a lexicographic row comparator; `partition.c` sorts `(key, member)` pairs by key. Both hand-written sorts are gone. Wired into Makefile and CMake; `tests/c/test_sort.c` checks order, stability and permutation.
6. **Single validation point.** `canon_solve` no longer re-checks objective, profile, encoding, order or degree equality; only the NULL checks remain. The policy lives in `canon_problem_create`. The internal `canon_p1_search_subset` keeps its own degree precondition as a module contract.
7. **One validation bitmap.** `canon_perm_validate_scratch(p, n, bitmap)` (no allocation; clears the bitmap on entry). `canon_group_explicit_create` allocates one bitmap for all generators. `canon_perm_validate` is now a wrapper that allocates.
8. **Grow-only workspace.** `canon_p1_search` records `cap` (per-point arrays) and `snap_alloc` (snapshot words). It reallocates only when a degree exceeds `cap`. Otherwise it calls `canon_partition_set_degree` (new; the partition also records its `cap`). `test_search_subset.c` runs degrees 4, 1, 3, 0, 2, 4 on one workspace and compares each result with a fresh workspace.
9. **`canon_group_alloc`.** New `src/bsgs/group.c` holds `canon_group_alloc(ops, degree, impl, out)` (count = 1 set there), `canon_group_share`/`unshare` and the public `canon_group_retain`/`release`. `explicit.c` calls it and no longer builds the handle. `ops->destroy` now frees only the backend state. `test_group_explicit.c` checks the count invariant and a share/unshare pair through a const pointer.
