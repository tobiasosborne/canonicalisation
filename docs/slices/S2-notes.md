# Slice S2 implementation notes

Brief: [`S2.md`](S2.md). Normative text: `docs/specification.md` v2.0 (cited as §x). Prerequisite: S1 ([`S1-notes.md`](S1-notes.md)), whose readings 1–12 still apply. This file records what was built, the readings taken of sentences that admit more than one implementation, the deviations from the brief, and what was left out.

## Files added

| Path | Content |
|---|---|
| `src/object/graph.h`, `graph.c` | coloured directed multigraph: §4.1 import and normalisation (colour and label tables in B order, duplicates combined, zero multiplicities invalid, arcs sorted by `(source, target, B(label))`, exact total multiplicity), CSR/CSC, §2.1 action into reusable storage, extensional equality, §4.1 simple undirected wrapper |
| `src/object/object.h`, `object.c` | root object `canon_root` (subset or graph): §7.1 initial key, action into a reusable `canon_root_image`, stream write and exact stream size by kind |
| `src/encoding/graph_stream.h`, `graph_stream.c` | CDAG-2 stream of a top-level graph (one record `09`) and its exact length with overflow checks |
| `src/encoding/simple_upper.h`, `simple_upper.c` | §4.4 `SIMPLE-UPPER-1` key (library function only) |
| `src/arena/alloc.h` | the one checked array allocator `canon_alloc_array` (review item 7) |
| `tests/c/test_nat.c` | `Nat` byte for byte for 0, 1, 255, 256, 65535, 65536, 2³²−1, 2³², 2⁶³, 2⁶⁴−1, 300 |
| `tests/c/test_graph.c` | import normalisation, label/colour order, invalid input and capacity, insertion-order independence, action, `(x^p)^q = x^(pq)`, equality, simple wrapper |
| `tests/c/test_graph_stream.c` | §7.4 one-arc case and the n = 1 loop case through the public API; hand-derived streams |
| `tests/c/test_signature.c` | sparse signatures and comparison against a dense implementation in the test (random graphs and partitions); O-stage split against a dense model; hand-built splits including §7.4's `[{1},{0}]` |
| `tests/c/test_simple_upper.c` | keys for n = 0..5 including padding; class rejections (one-way arc, loop, doubled arc, labels, colours) |
| `tests/c/test_search_graph.c` | §7.4 graph case through the API (trace, bytes, witness `[1,0]`, output arc `1 -> 0`), node and output capacity limits, a graph fixed by its group, equivariance and witness validity on random graphs, invalid input, simple wrapper, workspace reuse across kinds |
| `docs/slices/S2-notes.md` | this file |

Files changed: `src/arena/checked.h` (`canon_u64_add`, `canon_u64_mul`, review item 6), `include/canon/canon.h` (`canon_arc`, `canon_object_create_graph`, `canon_object_create_simple_graph`, problem documentation), `src/encoding/wire.h`, `wire.c` (`canon_buf_put_nat`, `canon_nat_length`), `src/refine/p1.h`, `p1.c` (root-generic refinement, grow-only scratch, O stage), `src/search/p1_tree.h`, `p1_tree.c` (root-generic search), `src/api/api.c` (objects are `canon_root`s, graph builders, exact output size by kind), `tools/canon-cli.c` (`p1-graph`), `tests/c/check.h` (`check_hex_is`), `tests/python/test_e2e.py` (graph tiers), `Makefile`, `CMakeLists.txt`, and the READMEs of `include/`, `src/`, `src/{api,arena,encoding,object,refine,search}/`, `tools/`, `tests/c/`, `tests/python/`. `.github/workflows/ci.yml` is unchanged: ctest registers the new C tests from `CMakeLists.txt`, and the Python job already runs `test_e2e.py`.

## Spec readings taken

1. **O-stage index order (§7.1).** "for arc labels sorted by B(label), then cell index j: outgoing multiplicity from v into C[j], incoming multiplicity from C[j] into v". Read as label-major, then cell, then the pair (out, in): position `((l·k)+j)·2+dir`, `dir = 0` out, `1` in. The brief and `review_checks.p1` (`for label in labels for cell in partition for count in (out, in)`) take the same reading.
2. **Which labels (§7.1).** L is the set of distinct labels of the normalised graph (all have positive multiplicity, §4.1). Any larger label universe would only insert all-zero blocks at the same positions of every signature in a stage, which changes no comparison, so the reading is immaterial to the result. The model uses the labels of its input arcs, the same set.
3. **Entry snapshot (§7.1).** "Within O all signatures refer to its entry snapshot". All n signatures are computed from the partition as it is when the stage starts (`cell_of`, `k`), and only then is `split` applied. The partition itself is therefore the snapshot and no copy is taken.
4. **Loops (§7.1).** "A graph loop contributes once to each incoming/outgoing count": a loop `v -> v` with label l adds its multiplicity to `out(v, l, cell(v))` from the CSR pass and to `in(v, l, cell(v))` from the CSC pass.
5. **Sparse comparison (§7.1).** "Count signatures compare lexicographically using the ordinary numerical order on mathematical naturals". Within a stage every dense vector has the same length `2·L·k`. The sparse rule (smaller next index means a positive entry where the other vector is zero, so that side is greater; equal indices compare counts; an exhausted list is all zeros) is exactly that comparison with zeros elided. The justification is in `src/refine/p1.c`. `test_signature.c` checks it against a dense implementation on 400 random graphs and partitions, and a mutation of the rule fails thousands of checks.
6. **Split by rank (§7.1).** "replaces each old cell, in its old position, by its nonempty signature classes in increasing lexicographic signature order". Signatures are ranked globally by exact comparison (never hashed, §10) and S1's `split` is called with the ranks as keys. Because the ranks respect the global order, the classes inside each cell come in increasing signature order.
7. **Initial key (§7.1).** "on a top-level graph it is B(vertex_colour[a])". B order compares length first, then unsigned bytes (§4.3). The colour table is sorted by `(length, bytes)`, so the colour id is the rank of the key and increasing id means increasing key.
8. **Graph record in the DAG (§4.1, §4.2).** The `09` record holds atom IDs but no child references, so it has height 0 and is the only record: `q = 1`, root `0`. The colours are written in vertex order `0..n-1` ("n values B(vertex_colour)"). The §7.4 graph stream confirms both.
9. **Combining and capacity (§4.1, §11.1).** Duplicate `(source, target, label)` arcs are combined by exact addition during import. The total positive multiplicity is computed exactly at the same time ("compute that bound exactly during import"). With the count-bit limit of 64 (detailed plan §2.1), a combined or total multiplicity above `uint64` is `CAPACITY_LIMIT`. Every O-stage count is a sum over a subset of the arcs and so is bounded by the total, so the O stage cannot overflow. Its overflow checks remain as defence. A colour or label longer than `2³²−1` bytes, or more than `2³²−1` distinct arcs, is `CAPACITY_LIMIT` ("Lengths and counts must fit U32; overflow is a capacity error"). `2·L·k` not fitting `uint64` is also `CAPACITY_LIMIT`, but that is unreachable in memory.
10. **Exact output size (§11.1).** "use a count-only canonical traversal/encoder or a conservative input-derived bound". The graph stream length is 15 + 1 + Σ_v (4 + |colour(v)|) + 4 + Σ_arcs (12 + |label| + |Nat(m)|) + 4. The action only renumbers vertices, so the colour multiset, the arc count, the label bytes and the multiplicities are unchanged. The input's length is therefore the exact length of every image. The length is measured once at import and cached on the graph (`stream_size`, review item 3), and images carry their source's value. `canon_problem_create` compares it with `max_output_bytes`, and `test_search_graph.c` checks the boundary at 48/49 and 49/50 bytes.
11. **Status precedence (graph builders).** As in S1 reading 10: NULL `ctx`/`out` or a missing required array → `INVALID_INPUT`; degree above `max_n` → `CAPACITY_LIMIT`; then the data. All invalid conditions are checked first (vertex out of range, zero multiplicity, NULL bytes with a nonzero length → `INVALID_INPUT`), then the capacity conditions (lengths, multiplicity overflow, arc count → `CAPACITY_LIMIT`). The spec does not fix an order.
12. **Simple wrapper (§4.1).** "Schema-specific wrappers for simple graphs reject loops and coalesce duplicate undirected edges before translating to two opposite unit arcs." `{a, b}` and `{b, a}` are the same edge; a loop or a vertex `≥ n` is `INVALID_INPUT`; colours and labels are empty.
13. **SIMPLE-UPPER-1 class (§4.4).** "(empty vertex/arc labels, no loops, and exactly one arc in each direction for each edge)". "Empty vertex labels" is read as empty vertex colours. The class is checked on the normalised graph: every arc has the empty label, no arc is a loop, every arc has multiplicity 1 (duplicates are already combined, so this means exactly one), and every arc's reverse exists. Anything else is `INVALID_INPUT` (brief). "Pad the final byte with zero low bits" is read as: when there are no pairs (n ≤ 1) there is no final byte, and the key is `U32(n)` alone. The model checks only the bit order (`run()`, as tuples), not the packing, so the n = 0..5 expectations in `test_simple_upper.c` are derived by hand.
14. **Witness.** S1 reading 1 carries over unchanged: the least attaining leaf witness of the unpruned tree is the §3 deterministic witness. `test_e2e.py` now also asserts `witness == min{g ∈ G : graph_bytes(x^g) = c}` on every G1 and G2 case.

## Deviations from the brief and why

- **`canon_p1_search_run`, not `canon_p1_search`.** The brief's function name collides with the existing workspace type `canon_p1_search` (S1). Renaming the type would have touched `api.c` for no gain.
- **`canon_root_act_into(x, g, img)` has no separate scratch argument.** The reusable storage, including the arc sort buffer, lives in the `canon_root_image` (and the graph's `arcs_tmp`). A kind switch frees the other kind's storage; capacities only grow.
- **O-stage scratch layout.** Per-vertex offsets are not stored. Vertex v's entries start at `out_start[v] + in_start[v]` (the CSR/CSC prefix sums), which partitions the `2e` entries exactly. Besides the brief's pairs buffer and ranks, the scratch holds `entries_tmp` (sort buffer), `sig_len` and `order`/`order_tmp` (vertex sort). Allocation of the refinement scratch moved from `p1_tree.c` into `p1.c` (`canon_p1_scratch_reserve`, grow-only; on failure the previous arrays are kept).
- **`canon_p1_initial(p, root, s)`** replaces S1's `canon_p1_initial_subset`. `canon_p1_refine_node` takes the root, and `canon_p1_stage_o`, `canon_p1_graph_signatures`, `canon_p1_signature` and `canon_p1_sig_compare` are exposed in `p1.h` for the tests.
- **NULL colour arrays mean all colours empty.** The brief allowed NULL colour arrays only for `degree == 0`. Since review item 5, `canon_object_create_graph` accepts both arrays NULL for any degree, as `canon_graph_init` always did. Exactly one of them NULL is `INVALID_INPUT`.
- **G1 runs both generating sets** (the brief asks for both only in G2). Both tiers scramble the CLI's arc input (shuffle, split multiplicities into duplicates) while the model gets the combined arcs. This is an end-to-end insertion-order test (§5).
- **G2 sampling.** For each `(source, target, label)` slot with `label ∈ {"", "a"}`, an arc is present with probability `density·2/3` (`density` drawn from {¼, ½, 1}) and has multiplicity 1 or 2. Overall multiplicities are therefore in {0, 1, 2}, as the brief requires. Colours are drawn from {"", "c"}. Each graph is seeded by `(n, group index, k)`. No reduction was needed: the whole Python suite runs in about 7 s (see timing).
- **`test_nat.c`** is a separate file rather than an extension of `test_wire.c` (the brief allows either).

## Spec/model agreement

No disagreement between the specification and `checks/review_checks.py` was found within the S2 scope. Evidence:

- G1: 664 CLI runs over every subgroup of `Sym(n)` for `n ≤ 2`, every multiplicity vector in {0,1,2}^(n·n) and both colourings.
- G2: 2880 CLI runs over every subgroup of `Sym(3)` and `Sym(4)`, with labels, colours and multiplicities.
- In both tiers, with two generating sets each, trace, bytes and witness are identical in C and in the model.
- The §7.4 graph entry of `golden.json` matches.
- The model's witness is the §3 deterministic witness on every case.

The points examined for a possible divergence were:

- **Label set and order.** The model uses `sorted({labels}, key=blob)`; this is B order (reading 2).
- **Duplicate arcs.** `p1` sums duplicate arcs inside the signature rather than combining them first, which gives the same counts.
- **Image arcs.** `act_object` does not re-sort the image arcs; `graph_bytes` sorts and combines them, so the bytes agree with §4.1.
- **Initial key.** `blob(colour)` is the B order of reading 7.

## Left out (later slices)

- The multi-limb `canon_nat` of detailed plan §2.1. Not needed while the count-bit limit is 64; a larger count is `CAPACITY_LIMIT`.
- The minimum search under `SIMPLE-UPPER-1`. `canon_problem_create` still returns `UNSUPPORTED_ACTION` for order `0x0002`; S4 adds it.
- The CDAG-2 decoder, `canon_object_create` from a stream, nested objects and the relations record `0a`; S5 adds them.
- Pruning (S7).
- Sparse-adjacency performance work (S8/M5). Each O sweep recomputes every signature: O(e log e) to build, then O(n log n) signature comparisons.
- Dense bitplane signatures (§10) are not implemented.

## Timing and environment

- `make check` without sanitizers: about 8.0 s wall-clock. This covers `review_checks.py` and 27 Python tests, of which `test_e2e.py` is about 7 s including about 3.5 k graph CLI runs. Under ASan/UBSan, `test_e2e.py` takes about 49 s (ctest `test_e2e` in the CMake sanitizer build).
- As in S1, the local clang has no ASan runtime. The clang build was also run with `-fsanitize=undefined -fsanitize-trap=all`, and all C tests and `test_e2e.py` passed.

## Review fixes

The S2 review found no crash-grade defect and ten findings. All ten are fixed on this branch. Every §5 command was then run again: the S1 and S2 tiers are unchanged and still agree byte for byte with the model.

1. **Trace first at a leaf** (`src/search/p1_tree.c`).
   - `evaluate_leaf` compares the leaf trace with the best trace before anything else (§7.2: "with trace compared first"). If the trace is greater, the leaf returns at once, without `t_L`, the image or its stream.
   - Otherwise the image and stream are built. An equal trace then breaks ties on the bytes and then on the witness, as before.
   - New counters `leaves` and `images` in `canon_p1_search`. `test_search_graph.c` checks them on a directed 2-cycle plus a directed 3-cycle under `Sym(5)`: 12 leaves, 6 images.
   - `test_e2e.py` compares the same graph with the model, which materialises every leaf.
2. **No CSR/CSC on images** (`src/object/graph.c`).
   - Index construction is now `canon_graph_build_index`. Import calls it; `canon_graph_act_into` does not.
   - The image type documents that its index is absent (`indexed = false`).
   - `canon_simple_upper_key` finds reverse arcs by binary search over the sorted arc array, so it needs no index and works on images too.
   - `canon_p1_graph_signatures` refuses an unindexed graph (`INTERNAL_ERROR`; the root is always imported).
   - `test_graph.c` builds the index of an image on demand and checks it.
3. **Cached stream length** (`src/encoding/graph_stream.c`).
   - `canon_graph_stream_measure` computes the exact length once, at import. It is stored in `canon_graph.stream_size`, and an image copies it with the tables.
   - `canon_graph_stream_size` returns the cached value, and `canon_graph_stream_write` reserves from it.
   - Import now also returns `CAPACITY_LIMIT` for a stream length above `uint64`.
   - `test_graph.c` checks that a fresh measurement of an image equals the cached value.
4. **No borrowed pointers after a run.**
   - `canon_p1_search_run` ends every run, on every status, with `canon_root_image_clear`. That calls `canon_graph_image_clear`, which zeroes the borrowed colour and label tables and sets `n = e = 0` while keeping the storage.
   - The invariant is documented on `canon_p1_search`.
   - `test_search_graph.c` checks the cleared state after a run. It also solves a labelled, coloured graph, releases the object and result, then reuses the workspace for another graph, after both a complete and an abandoned (node-quota) run, and compares with a fresh workspace. This passes under ASan.
5. **NULL colours for any degree.**
   - `canon_object_create_graph` accepts `colours == colour_lengths == NULL` for `degree > 0` as "all colours empty"; exactly one NULL is `INVALID_INPUT`. The header comment is updated.
   - `canon-cli p1-graph` passes NULL for the default colours. It allocates colour arrays only when `--colours` is given, after checking that it lists exactly N entries, so N is bounded by the argument length.
   - `--n 4294967295` now reports `CAPACITY_LIMIT` deterministically, like `p1-subset`. `test_e2e.py` covers both subcommands.
6. **One checked `uint64` addition.** `canon_u64_add` and `canon_u64_mul` live in `src/arena/checked.h` and are used for:
   - the stream length (`graph_stream.c`);
   - the total multiplicity at import (`graph.c`);
   - the signature count merge and the `2·L·k` range check (`p1.c`).
7. **One checked array allocator.**
   - `src/arena/alloc.h` provides `canon_alloc_array(count, size, &status)`. It allocates max(count, 1) elements; a size overflow is `CAPACITY_LIMIT` and a failed `malloc` is `RESOURCE_LIMIT`. The convention is stated once, in that header.
   - It returns `void *`, which also removes the old `(void **)&ptr` casts.
   - It replaces the graph's private `alloc_array`, the subset image allocation in `object.c`, the scratch allocations in `p1.c` and the witness arrays in `p1_tree.c`'s `prepare`.
   - S1 allocation sites outside the review's list (`subset.c`, `partition.c`, `explicit.c`, the handle and result allocations in `api.c`) are unchanged.
8. **Per-point scratch as one unit.**
   - The seven per-point arrays are grouped in `canon_p1_points` and allocated as one block.
   - `points_alloc` carves the block from a single table of the `uint32_t` array fields, after the `size_t` array for alignment.
   - The arrays are swapped wholesale on growth and freed with one `free`. Adding an array touches that table and the struct only.
   - The signature entries and their sort scratch are likewise the two halves of one allocation.
9. **No defensive free in the action.**
   - `canon_graph_act_into` no longer frees anything belonging to `dest`.
   - The precondition (image storage from `canon_graph_init_empty`) is stated in `graph.h` and checked with the new `imported` flag, which replaces `borrowed_tables`.
   - An imported `dest`, or `dest == g`, is refused with `INVALID_INPUT` and left untouched (tested).
10. **Single-pass validation.**
    - `canon_graph_init` validates in one pass over the colours and one over the arcs. Each pass records both an invalid flag and a capacity flag, and stops at the first invalid item.
    - Invalid input is returned before capacity, so the documented order is unchanged.

No finding was disputed. One adjustment, made while fixing item 2: the review suggested that `canon_simple_upper_key` call the index builder. It takes a `const` graph, so it instead does a binary search over the sorted arcs, which needs no index at all.
