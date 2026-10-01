# Slice S2 implementation notes

Brief: [`S2.md`](S2.md). Normative text: `docs/specification.md` v2.0 (cited as §x). Prerequisite: S1 ([`S1-notes.md`](S1-notes.md)), whose readings 1–12 still apply. This file records what was built, the readings taken of sentences that admit more than one implementation, the deviations from the brief, and what was left out.

## Files added

| Path | Content |
|---|---|
| `src/object/graph.h`, `graph.c` | coloured directed multigraph: §4.1 import and normalisation (colour and label tables in B order, duplicates combined, zero multiplicities invalid, arcs sorted by `(source, target, B(label))`, exact total multiplicity), CSR/CSC, §2.1 action into reusable storage, extensional equality, §4.1 simple undirected wrapper |
| `src/object/object.h`, `object.c` | root object `canon_root` (subset or graph): §7.1 initial key, action into a reusable `canon_root_image`, stream write and exact stream size by kind |
| `src/encoding/graph_stream.h`, `graph_stream.c` | CDAG-2 stream of a top-level graph (one record `09`) and its exact length with overflow checks |
| `src/encoding/simple_upper.h`, `simple_upper.c` | §4.4 `SIMPLE-UPPER-1` key (library function only) |
| `tests/c/test_nat.c` | `Nat` byte for byte for 0, 1, 255, 256, 65535, 65536, 2³²−1, 2³², 2⁶³, 2⁶⁴−1, 300 |
| `tests/c/test_graph.c` | import normalisation, label/colour order, invalid input and capacity, insertion-order independence, action, `(x^p)^q = x^(pq)`, equality, simple wrapper |
| `tests/c/test_graph_stream.c` | §7.4 one-arc case and the n = 1 loop case through the public API; hand-derived streams |
| `tests/c/test_signature.c` | sparse signatures and comparison against a dense implementation in the test (random graphs and partitions); O-stage split against a dense model; hand-built splits including §7.4's `[{1},{0}]` |
| `tests/c/test_simple_upper.c` | keys for n = 0..5 including padding; class rejections (one-way arc, loop, doubled arc, labels, colours) |
| `tests/c/test_search_graph.c` | §7.4 graph case through the API (trace, bytes, witness `[1,0]`, output arc `1 -> 0`), node and output capacity limits, a graph fixed by its group, equivariance and witness validity on random graphs, invalid input, simple wrapper, workspace reuse across kinds |
| `docs/slices/S2-notes.md` | this file |

Files changed: `include/canon/canon.h` (`canon_arc`, `canon_object_create_graph`, `canon_object_create_simple_graph`, problem documentation), `src/encoding/wire.h`, `wire.c` (`canon_buf_put_nat`, `canon_nat_length`), `src/refine/p1.h`, `p1.c` (root-generic refinement, grow-only scratch, O stage), `src/search/p1_tree.h`, `p1_tree.c` (root-generic search), `src/api/api.c` (objects are `canon_root`s, graph builders, exact output size by kind), `tools/canon-cli.c` (`p1-graph`), `tests/c/check.h` (`check_hex_is`), `tests/python/test_e2e.py` (graph tiers), `Makefile`, `CMakeLists.txt`, and the READMEs of `include/`, `src/`, `src/{api,encoding,object,refine,search}/`, `tools/`, `tests/c/`, `tests/python/`. `.github/workflows/ci.yml` is unchanged: ctest registers the new C tests from `CMakeLists.txt`, and the Python job already runs `test_e2e.py`.

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
10. **Exact output size (§11.1).** "use a count-only canonical traversal/encoder or a conservative input-derived bound". The graph stream length is 15 + 1 + Σ_v (4 + |colour(v)|) + 4 + Σ_arcs (12 + |label| + |Nat(m)|) + 4. The action only renumbers vertices, so the colour multiset, the arc count, the label bytes and the multiplicities are unchanged. The input's length is therefore the exact length of every image. `canon_problem_create` compares it with `max_output_bytes`, and `test_search_graph.c` checks the boundary at 48/49 and 49/50 bytes.
11. **Status precedence (graph builders).** As in S1 reading 10: NULL `ctx`/`out` or a missing required array → `INVALID_INPUT`; degree above `max_n` → `CAPACITY_LIMIT`; then the data. All invalid conditions are checked first (vertex out of range, zero multiplicity, NULL bytes with a nonzero length → `INVALID_INPUT`), then the capacity conditions (lengths, multiplicity overflow, arc count → `CAPACITY_LIMIT`). The spec does not fix an order.
12. **Simple wrapper (§4.1).** "Schema-specific wrappers for simple graphs reject loops and coalesce duplicate undirected edges before translating to two opposite unit arcs." `{a, b}` and `{b, a}` are the same edge; a loop or a vertex `≥ n` is `INVALID_INPUT`; colours and labels are empty.
13. **SIMPLE-UPPER-1 class (§4.4).** "(empty vertex/arc labels, no loops, and exactly one arc in each direction for each edge)". "Empty vertex labels" is read as empty vertex colours. The class is checked on the normalised graph: every arc has the empty label, no arc is a loop, every arc has multiplicity 1 (duplicates are already combined, so this means exactly one), and every arc's reverse exists. Anything else is `INVALID_INPUT` (brief). "Pad the final byte with zero low bits" is read as: when there are no pairs (n ≤ 1) there is no final byte, and the key is `U32(n)` alone. The model checks only the bit order (`run()`, as tuples), not the packing, so the n = 0..5 expectations in `test_simple_upper.c` are derived by hand.
14. **Witness.** S1 reading 1 carries over unchanged: the least attaining leaf witness of the unpruned tree is the §3 deterministic witness. `test_e2e.py` now also asserts `witness == min{g ∈ G : graph_bytes(x^g) = c}` on every G1 and G2 case.

## Deviations from the brief and why

- **`canon_p1_search_run`, not `canon_p1_search`.** The brief's function name collides with the existing workspace type `canon_p1_search` (S1). Renaming the type would have touched `api.c` for no gain.
- **`canon_root_act_into(x, g, img)` has no separate scratch argument.** The reusable storage, including the arc sort buffer, lives in the `canon_root_image` (and the graph's `arcs_tmp`). A kind switch frees the other kind's storage; capacities only grow.
- **O-stage scratch layout.** Per-vertex offsets are not stored. Vertex v's entries start at `out_start[v] + in_start[v]` (the CSR/CSC prefix sums), which partitions the `2e` entries exactly. Besides the brief's pairs buffer and ranks, the scratch holds `entries_tmp` (sort buffer), `sig_len` and `order`/`order_tmp` (vertex sort). Allocation of the refinement scratch moved from `p1_tree.c` into `p1.c` (`canon_p1_scratch_reserve`, grow-only; on failure the previous arrays are kept).
- **`canon_p1_initial(p, root, s)`** replaces S1's `canon_p1_initial_subset`. `canon_p1_refine_node` takes the root, and `canon_p1_stage_o`, `canon_p1_graph_signatures`, `canon_p1_signature` and `canon_p1_sig_compare` are exposed in `p1.h` for the tests.
- **Internal `canon_graph_init` accepts NULL colour arrays** (meaning all colours empty), for the simple wrapper and the tests. The public `canon_object_create_graph` keeps the brief's contract: both arrays are required when `degree > 0`.
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
