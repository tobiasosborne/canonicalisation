# Fix lane R2-ledger (B-kernel-02, -07, -08)

Branch: `t3/ledger-decoder-arena-fixes`. Base includes K1 merge `26b28ce`.

## B-kernel-02 — §11.2 ledger counts payload only

- Confirmed by reading `src/arena/mem.c` (`allocate`/`realloc`/`free` charged only
  `bytes`) and by a probe linked against the pre-fix library: 64 blocks of 1 byte
  raised `live_bytes` by exactly 64; a 0-byte block raised it by 0.
  Spec §11.2 (docs/specification.md:373-383): "At every phase account for:
  `M_live = ... + M_allocator_overhead`" and "Reclamation lag counts".
- Change:
  - `src/arena/mem.c`/`mem.h`: live/peak now charge payload + `sizeof(canon_mem_header)`
    (32 B here: `size_t` padded to `max_align_t`) + a documented 32 B per-block
    system-allocator allowance (`CANON_MEM_SYSTEM_OVERHEAD`: chunk size word + alignment
    rounding + minimum-chunk slack on common allocators; a stated upper allowance, not a
    measurement of a specific allocator). New `canon_mem_block_overhead()` (= 64 here)
    exposes the per-block charge. Stored `h->size` stays payload; realloc delta is the
    payload delta; free uncharges payload + overhead; a 0-byte block charges 64.
  - `include/canon/canon.h`: `live_bytes` documented as charged bytes, not payload.
  - `src/api/api.c` admission plan (§11.1), same units: new `chain_bytes_charged()`
    adds the chain struct and one `canon_mem_block_overhead()` per block a chain owns
    (levels array, each used level's orbit block + `gen_ids`, 3 perm tables,
    `gen_node`/`inv_node`, provenance nodes) to `canon_bsgs_bytes()` payload; used for
    the root group and for DAG leaf chains. Explicit backend adds 4 block overheads.
    The fixed plan term keeps its block-count allowance (1024 + 16/point) but prices
    each block at `canon_mem_block_overhead()` instead of 16 B. `src/bsgs` untouched
    (lane restriction): the unit conversion lives in the plan, its only consumer.
  - Status effect: planned bytes increase (they now count real charged bytes). Under
    the default budget (2^32) no tier input changes status (`test_metrics`: all 1534
    solves admitted, `peak <= plan` on every one). An explicit `max_live_bytes` set
    between the old payload-only plan and the new charged plan for the same problem
    now refuses with `CAPACITY_LIMIT` where it previously admitted; that is the
    §11.2 unit correction itself (the old plan understated the bytes the budget
    names), not a semantic change. No output, trace, witness or node count changes.
- Test: `ledger_overhead()` in `tests/c/test_metrics.c` (exact charged deltas for
  malloc/0-byte/realloc/free); whole `test_metrics` passes, worst peak/plan
  4209976/7186500. Post-fix probe: `delta=4160` (= 64×65), `zero_delta=64`.

## B-kernel-07 — decoder O(n) bit maps up front

- Confirmed by reading `canon_cdag_decode` (allocated `canon_cdag_bits_words(n)`
  unconditionally after the header; only Perm/Group/Coset readers use the maps) and by
  the new test failing pre-fix (peak ≥ 1 MiB bound exceeded by the 64 MiB maps).
- Change (`src/encoding/cdag_decode.c` only): `decode_records` owns a lazy
  `ensure_bits()` holder, allocated and zeroed on the first Perm/Group/Coset record and
  freed on exit. When needed, the size is still bounded by the caller's admitted
  `max_n` (§11.1, checked against the header before any record). The exported readers'
  bits contract and `src/object/dag.c`'s own scratch are unchanged.
- Status effect: none for well-resourced runs (same statuses/reasons/bytes; whole
  `test_cdag_decode` passes). Under allocation failure, a stream with no Perm/Group
  record no longer fails at the up-front maps allocation, and the allocation-request
  count drops by one for such streams (fault-injection `k` numbering shifts
  accordingly; `test_faults` derives `k` from the live request count).
- Left as-is, with reason: a short stream that *does* contain a Perm/Group record
  mentioning a high point still allocates the O(n) maps. That size is the caller's
  admitted `max_n` scratch (public default 4096 → 1 KiB), and avoiding it would need a
  sparse-map redesign of the exported readers used by `src/object/dag.c`; the finding
  (severity low) names lazy allocation as the remedy and its test is the literal case.
- Test: `test_lazy_bits()` in `tests/c/test_cdag_decode.c` — 24-byte stream, n = 2^28,
  one empty literal, `max_n = UINT32_MAX`: decode COMPLETE with ledger peak < 1 MiB
  (failed pre-fix, passes post-fix).

## B-kernel-08 — `canon_dag_normalise` trusts payloads

- Confirmation: pending.

## Tests run

- Pre-fix probe `/tmp/probe_ledger.c` (not committed): `delta=64`, `zero_delta=0`.
- `nice -n 19 ./build/make/test_metrics`: ok (1534 solves, peak ≤ plan on all).
- `test_cdag_decode`: new `test_lazy_bits` failed pre-fix (1 failure), ok post-fix.

## Left undone

- B-kernel-08.
