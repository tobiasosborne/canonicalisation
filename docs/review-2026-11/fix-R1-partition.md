# Fix lane R1-partition: E-perf-refine-02 and -03

Branch: `t3/partition-counting-split`. Brief: `FIX-BRIEF.md` in this directory.
Output must be byte-identical (PROFILE-EQUIV); light testing only. No push.

Commits (oldest first):

| Commit | What |
|---|---|
| `6062878` | this report's skeleton: confirmed evidence, baseline timing |
| `fa92030` | differential split test (old algorithm as test-only reference) |
| `f6214eb` | E-perf-refine-02: stable counting split in `canon_partition_split` |
| `0f1f78f` | high-degree duplicate-heavy signature test |
| `bbdc345` | E-perf-refine-03 option 1: typed signature-entry sort |

## E-perf-refine-02 (= D-perf-07, partition part) — counting split

### Confirmed in code (before the fix)

- `src/partition/partition.c:101-125` (old numbering): per cell, gathers
  `(sig[lab[j]], lab[j])` pairs, stable merge sorts them through the generic
  `canon_stable_sort` (`src/util/sort.c`: `void *` elements, indirect comparator
  per comparison, `memcpy` per element per pass), writes `lab` back, then
  `rebuild_index` walks every cell again writing `pos`/`cell_of` — a second
  scattered pass over data the split just touched, paid on every split
  (including no-change final sweeps).
- All three production call sites hand it dense bounded keys in `[0, n)`:
  initial keys 0/1 or colour ranks (`src/refine/p1.c:131`, old numbering),
  O-stage ranks `0..R-1`, `R <= n` (`p1.c:297`), G-stage orbit ids, dense by
  construction (`p1.c:352`).
- Spec support: §10 "Use flat `lab`, `pos`, `cell_of`, cell spans/order,
  count/stamp arrays and touched lists" and "Signature sorting may use
  fixed-width radix or comparison sort, but hashes never order semantic
  classes"; §7.1 defines `split(P, sig)` cell-locally.

### Change (`src/partition/partition.{c,h}`, `src/partition/README.md`)

`canon_partition_split` now gathers each cell's `(key, member)` pairs with
min/max, then per cell:

- **Singleton or constant-key cell**: output equals input (the stable order of
  one class is the input order). No scatter at all; `cell_of` is renumbered
  only when an earlier cell in the same split actually subdivided. This makes
  the common final (no-change) sweep one sequential gather pass.
- **Counting path** when the cell's key range `R = max - min + 1` satisfies
  **`R <= 4 * (cell size)` and `R <= buckets_cap`** (the `pairs_tmp` entries
  allocated for the partition's degree capacity): zero the buckets, count
  frequencies, prefix over keys in increasing key order (§7.1) assigning each
  nonempty class its destination segment and output cell index (which *is*
  the new `start[]` segment), then a stable scatter in input order writing
  `lab`, `pos[member]` and `cell_of[member]` at placement. Cost
  `O(cell size + R)` per cell, `O(n)` for the split.
- **Fallback**: the previous algorithm (stable merge sort through
  `canon_stable_sort`), unchanged except that `pos`/`cell_of` are written
  during write-back.

Threshold rationale (stated in the code): counting costs two passes over `R`
(zero, prefix) plus count/scatter passes over the cell, all plain integer
ops, i.e. `O(s + R)`. With `R <= 4s` this is `O(s)` with a small constant
independent of `log s`, and the whole split stays `O(n)`. Beyond `4s` the
zero/prefix passes over a mostly-empty range cost more than the merge sort's
`O(s log s)`, whose cost does not depend on key sparsity, so sparse cells
fall back. The factor 4 is an implementation choice under spec §10; both
paths produce the unique stable ordering by `(key, old position)`, so the
choice is unobservable in the output.

Why output is bit-identical: stable counting sort and stable merge sort of
the same sequence by the same key produce the unique ordering by
`(key, input position)`, so `lab` is bit-identical, not just size-identical;
class boundaries sit between distinct keys in increasing key order within
each old cell's segment (§7.1 verbatim); placement-time `pos`/`cell_of`
writes give exactly the values `rebuild_index` computed. Split no longer
calls `rebuild_index` at all.

Scratch and the §11.2 ledger: the buckets reuse `pairs_tmp`
(`pairs_tmp[t].key` = count/cursor, `.member` = output cell index; the two
uses never overlap within one cell). `pairs`/`pairs_tmp` are the partition's
existing grow-only workspace scratch, allocated for the degree capacity at
`canon_partition_init` through `src/arena/mem.h` and reused for every degree
up to it (the search workspace re-inits only for a larger degree,
`src/search/p1_tree.c` `prepare`). **No new allocations, no allocation in
split, no size change**: the partition still costs the 36n + 8 bytes the
admission plan charges (`src/api/api.c` `plan_bytes`), fault-injection
allocation counts are unchanged, and `test_metrics` (peak <= plan) and
`test_faults` pass in the full `make test` below.

### Tests

`tests/c/test_partition.c` gained a differential test: the old algorithm is
kept as a test-only reference (`ref_split`: stable insertion sort of the
pairs, boundaries where the key changes, `pos`/`cell_of` rebuilt from the
result — the `(key, input position)` ordering is unique, so it reproduces
the old merge-sort split element for element; it was run against the old
production code first and passed). 420 trials: shuffled `lab`; partition
shapes single-cell / all-singletons / random boundaries; `n` in
{0, 1, 2, 3, 5, 17, 64, 96}; key shapes ties on 0..2 (counting path), dense
0..n-1, constant, maximal `uint32` ranges `{0, 1, 2^31, 2^32-2, 2^32-1}`
(fallback path), large base with small range (`0xffffff00 + {0,1,2}`),
sorted and reverse along `lab`. `lab`, `start`, `pos`, `cell_of`, the cell
count and the return value must match exactly, plus the §10 invariants.
Result: `test_partition: ok` before and after the production change.

### Measurement (one `canon-cli` invocation per number)

Hand-built graph: undirected cycle `C_n` (each edge as two directed arcs,
empty label, multiplicity 1), group = dihedral group `D_n` (rotation
`i -> i+1 mod n` and reflection `i -> -i mod n`),
`canon-cli p1-graph --prune off`, wall time under `nice -n 19` on a
contended machine (single invocations; read as indicative, not precise).
(A path under the cyclic group and the cycle under the cyclic group finish
in ~0.01-0.03 s at n = 200-300 because the G stage discretises at depth 1;
the dihedral cycle keeps mirror pairs, so the unpruned search visits O(n)
nodes and the run is long enough to time.)

| n | before (-02 old split) | after -02 | after -02 + -03 | stdout sha256 (all runs) |
|---|---|---|---|---|
| 300 | 0.248 s | — | — | (before only) `50bcaf2b…014d5c` |
| 500 | 0.660 s | 0.483 s | 0.483 s | `971f4d7d1786c0cd9833ff42064bf5f700938cff0fb3e97b8ddecde1eb90e350` |

Status `COMPLETE` and the stdout sha256 are identical before/after at
n = 500 (60191 bytes of record, trace included). The 500-vertex run took
0.660 s before and 0.483 s after the counting split (~27% less on this
single pair of invocations; split is only part of the work — `tuple_min`,
signature building and trace emission are untouched by -02, and this graph's
vertices have only 4 signature entries each, so -03 shows no further change
here).

## E-perf-refine-03 (option 1) — typed signature-entry sort

Confirmed: `canon_p1_graph_signatures` (`src/refine/p1.c`, old line 235)
called the generic `canon_stable_sort` once per vertex per O sweep to sort
`d_v` entries by index before merging duplicates — type-erased elements, an
indirect comparator per comparison, 16-byte `memcpy` per element per pass,
called even for `d_v` of 0 or 1.

Change (commit `bbdc345`, `src/refine/p1.c` only, inside signature
building): `entry_cmp` and that one call are replaced by `sig_entries_sort`,
a typed stable sort by `.index` — stable insertion sort for `len <= 16`
(the bounded-degree case; no scratch, no indirect call), and the same
bottom-up stable merge as `src/util/sort.c` but typed above that (direct
field compares, whole-entry assignment moves; tail runs by `memcpy`).
The merged signature is a pure function of the entry multiset (spec §10:
duplicate updates accumulate exactly; all counts are positive, so every
partial sum is <= the total and the checked-add overflow at the call site
depends only on the total), hence byte-identical output. The global vertex
rank sort in `canon_p1_stage_o` still uses `canon_stable_sort`; **`tuple_min`
and the G-stage code in `canon_p1_refine_node` were not touched** (another
lane's territory).

Tests: existing `tests/c/test_signature.c` checks every sparse signature
against a dense implementation on 400 random graphs. It gained
`test_high_degree` (commit `0f1f78f`): all 24 arcs incident to vertex 0
(out/in/loops/parallel arcs, multiplicities 1..3), asserting from the
CSR/CSC starts that vertex 0's pre-merge list exceeds 16 entries (so the
typed merge path is exercised deterministically) and that signatures match
the dense oracle on a nontrivial partition. `test_signature: ok` before and
after. The -03 radix option (one global pass over all vertices) and the
secondary arc order at import were not done (see Left undone).

## Test runs (light, per FIX-BRIEF; each listed once per run)

- `nice -n 19 make build/make/test_partition`: ok against the old split
  (validates the differential reference), then after -02.
- `test_partition`, `test_signature`, `test_search_subset`,
  `test_search_graph` after -02: all ok. `test_signature`,
  `test_search_graph`, `test_partition` again after -03: all ok.
- One `nice -n 19 make test` (the brief's allowance, after both fixes):
  **passed** — all C tests including `test_metrics` (1534 solves,
  peak <= plan), `test_faults`, `test_prune`, `test_certificate`, plus the
  fuzz replay (`fuzz_cdag_decode`: 12006 inputs, `fuzz_checker`: 4002
  inputs, no crash).
- No `make check`, `ctest`, corpus, oracle, ref runners or Python tiers were
  run, per the brief. Byte identity at CLI level is covered by the sha256
  above; node-count/profile identity rests on the bit-identical `lab`
  argument plus the differential test and the C search tests.

## Left undone

- `canon_partition_individualise` and `canon_partition_restore` still call
  `rebuild_index` (Theta(n) per explored child edge). The report's remedies
  (cell handles behind `cell_of`, snapshots widened with `pos`) change the
  snapshot layout and every `cell_of` reader in `src/refine`/`src/search`;
  the lane task scoped -02 to `canon_partition_split`, so they are left as
  they are. Split itself no longer pays the pass.
- E-perf-refine-01 (fused per-cell O split; signatures only for non-singleton
  cells — also the second half of D-perf-07) not attempted: it restructures
  `canon_p1_stage_o` around the G-stage boundary and needs the singleton
  count-overflow argument restated; outside this lane's brief.
- E-perf-refine-03 options 2 (one global radix pass over all vertices) and 3
  (secondary arc order at import, needs admission-plan room): not done. The
  insertion-sort cut (16) is untuned, as the report suggests deferring tuning
  to measurement under milestone M5.
- Threshold factor 4 is likewise untuned; it is output-unobservable and can
  be retuned freely when profiles exist.
