# Slice S8 implementation notes

Brief: [`S8.md`](S8.md) (written in this session; decisions D15–D18 are the implementer's and await the maintainer's review). Normative text: `docs/specification.md` v2.1 (cited as §x). Prerequisites: S1–S7b. Implemented 13 October 2026 by one agent on the maintainer's instruction ("work by yourself; grind through the next steps"); self-review below.

## Files added

| Path | Content |
|---|---|
| `src/arena/mem.h`, `mem.c` | The one allocator of the library and the live-memory ledger (§11.2): size headers, atomic live/peak/allocation/free/failure counters, `canon_mem_peak_reset`, `canon_mem_fail_after` (fault injection), `canon_mem_requests` |
| `fuzz/fuzz_driver.h`, `fuzz_cdag_decode.c`, `fuzz_checker.c`, `run.sh`, `README.md` | Fuzz targets with the libFuzzer entry point and a deterministic replay driver (D18) |
| `tests/c/test_sink.c` | Pause at every offset and resume; failure modes; the signed `00` payload |
| `tests/c/test_batch.c` | `canon_solve_batch` against separate solves on every T1 objective; the six edge tests of `S8-batch-reading.md` §5 |
| `tests/c/test_metrics.c` | Counters of the §7.4 cases; the ledger; plan ≥ measured peak on every tier and objective; `max_live_bytes` admission |
| `tests/c/test_faults.c` | Every k-th allocation request fails once, on a flow over every objective: clean statuses, no leak, releasable handles |
| `docs/slices/S8.md`, `S8-notes.md` | Brief and this file |

Files changed:

- Every `.c`/`.h` under `src/` that allocated: `malloc`/`calloc`/`realloc`/`free` → `canon_mem_*` (26 files; `arena/alloc.h` and `encoding/wire.c` included). The checker (`checker/`) is untouched: it has its own allocator and must share nothing with `src/`.
- `include/canon/canon.h`: `canon_capacity.max_live_bytes`; `canon_memory_stats`, `canon_memory_get`; `canon_metrics`, `canon_result_metrics`, `canon_problem_planned_bytes`; `canon_solve_batch` documented as implemented (spec §17 v2.1); `canon_result_encode_resume`; the sink contract's pause (`CANON_CANCELLED`).
- `src/api/api.c`: the admission plan (`plan_bytes`) and its check at problem creation; metrics collection in every solve path (`metrics_p1`, `metrics_enumeration`, the ledger bracket); `canon_solve_batch`; `canon_result_encode_resume` (`canon_result_encode` is the offset-0 call); the accessors. `src/api/stubs.c`: `canon_solve_batch` removed (checkpoints remain).
- `src/bsgs/chain.h`, `chain.c`: `canon_bsgs_bytes` (the measured size of a chain, the plan's group term).
- `src/symmetry`: the `fixing` counter; `src/search/p1_tree.c`: the reference run counts the generators that fix `x` (action check only) for the §7.3 singleton-orbit indicator.
- `tests/c/test_version.c` (the batch stub is gone), `test_search_subset.c` (a pause is `CANCELLED`), `test_chain.c`, `test_verify.c`, `test_search_dag.c` (hand-built library storage now comes from `canon_mem_*`, since the library frees it), every positional `canon_capacity` initialiser (the new field).
- `Makefile`, `CMakeLists.txt`: `mem.c`, the four tests, the fuzz targets (`make fuzz`, `make test` runs the replay with `FUZZ_ITER`, ctest `fuzz_replay`).
- READMEs (`src/arena`, `src/metrics`, `src/api`, `include`, `tests/c`, `fuzz`), `README.md`, `HANDOFF.md`, `CLAUDE.md`, `docs/detailed-implementation-plan.md`.

## Readings taken

1. **A pause has no status of its own (§3.2, §17).** The eight statuses are frozen; a paused sink reports `CANON_CANCELLED`, the "interrupted" status, with the offset retained in `*offset` for `canon_result_encode_resume`; `canon_result_encode` pauses the same way (S1 treated a pause as `OUTPUT_ERROR`). D15.
2. **Admission by plan, measurement by ledger (§11.1).** "Exceeding that policy yields `CAPACITY_LIMIT`, a function of semantic input, objective and descriptor, independent of worker count or lucky early discovery": the policy is the plan computed at problem creation from the input, never the measured peak, which depends on the allocator and on the workspace's history (grow-only storage). The plan is deliberately loose (quadratic stacks for a depth-`n` path; a chain estimate); the measured peak is reported in the metrics and `tests/c/test_metrics.c` checks `peak ≤ plan` on every tier. D16.
3. **The chain term of the plan is an estimate.** The strong generator count of a verified chain of a subgroup has no useful closed bound (the Schreier–Sims insertions are bounded by `depth · log₂|G|`, uselessly loose at degree 4096), so the plan charges 4 × the group's measured chain bytes plus 64 (log₂|G| + 1) dense rows; the tests calibrate it (largest measured peak/plan ratio on the tiers: 5 288 / 30 386 bytes). This is recorded in the brief and in `plan_bytes`'s comment as calibrated, not proved.
4. **Default budget 4 GiB.** The quadratic terms of the plan reach about 0.8 GB at the default `max_n` of 4096, so a 1 GiB default would refuse degrees the descriptor otherwise admits; `1 << 32` admits every degree up to the default `max_n` with a group of moderate chain size.
5. **Batch (§17 v2.1, D12).** Implemented literally: a batch of 0 is `COMPLETE` with nothing to write, so it is accepted even with NULL arrays; NULL arrays with `count > 0` are the call-level `INVALID_INPUT`; the byte-size overflow check covers both arrays; each entry is `canon_solve` on the one workspace, so a NULL entry is `INVALID_INPUT` with a NULL result, and admission and quota are the problem's own. Cancellation across a batch is not offered (not decided).
6. **Metrics of a status-only result (§3.2).** The counters describe the work done before the status was reached (the P1 nodes visited before `CAPACITY_LIMIT`), with zero output and certificate bytes; the brief's draft said "zero", the implementation and `canon.h` say this.
7. **Singleton-orbit indicator (§7.3).** "If every generator fixes x" is decided by the exact action on every generator, also on the unpruned reference run (where nothing is inserted into `A_known`); the root-discrete indicator is "the P1 tree has one node".
8. **Fuzzing without libFuzzer (D18).** This container's clang has no fuzzer runtime (`libclang_rt.fuzzer` missing), and HANDOFF forbids downloads, so the targets keep the `LLVMFuzzerTestOneInput` entry for machines that have it and are replayed here with deterministic mutations. The first replay run found a defect in the driver itself (a shrinking buffer after a truncating mutation, caught by glibc's fortify), fixed by fixed capacities.

## Deviations from the brief

- The metrics of a status-only result keep the counters (reading 6).
- A batch of 0 inputs with NULL arrays is accepted (there is nothing to write; the brief's wording "NULL arrays with count > 0" already implies it).
- No `tests/python` change: the CLI stays one case per process (brief §3 last item).

## Measured counters

- `test_batch`: 179 batches, 1 253 entries identical to separate solves (status, flags, trace, bytes, group bytes, key, certificate, witness, sign, counters).
- `test_metrics`: 1 284 solves on the T1 subsets, seeded digraphs and every objective (with certificates and deterministic witnesses): measured peak ≤ plan on every one; the largest peak/plan ratio 5 288 / 30 386 bytes; the §7.4 empty subset under `Sym(2)`: 3 nodes, 2 leaves unpruned; 2 nodes, 1 pruned child, 1 automorphism inserted under the default policy; stream 24 bytes.
- `test_faults`: a flow over every objective makes 1 768 allocation requests; 1 768 faulted runs each ended with `RESOURCE_LIMIT` on the faulted step and `COMPLETE` elsewhere (63 749 complete steps in all), no other status, the ledger back at its baseline after every run, no sanitizer finding.
- Fuzz replay: 12 006 inputs through the decoder target and 4 002 through the checker target, no crash (2 000 mutations per seed) after the fix below.
- **Finding (checker, fixed).** The checker target under ASan found a mutated certificate whose chain record named about 2^32 input generators; `cc_chain_read` sized the `i x n` array before checking that the stream could back it, and ASan's allocator aborted on the 17 GB request (plain `malloc` would have returned NULL and the reason `OUT_OF_MEMORY`, a verdict that depends on the machine). Every array the checker sizes from a stream count is now bounded first by the bytes that must follow it (`TRUNCATED`: the FINAL witness backs `n`, stored generators back `g`, dense rows back `k`, `m` and `i`, a tag byte backs every CDAG-2 record, 16 bytes back every arc), and `cc_alloc` caps a single allocation at `CC_MAX_ALLOC` (1 GiB, `CAPACITY`), which only the `p x n` provenance evaluation can reach. The 20 checker harness tests and every certificate tier still pass.

## Self-review

1. `canon_mem_realloc` keeps the old block on failure and counts the size difference exactly; `canon_mem_free` reads the header before freeing; a NULL is a no-op everywhere.
2. The allocator migration was mechanical (regular expressions over `src/`), then every test that hands storage to a library struct was moved to `canon_mem_*`; the `free(): invalid pointer` aborts of the first run were exactly those tests (`test_chain`, `test_verify`, `test_search_dag`), and the ledger's `live == baseline` check in `test_metrics` and `test_faults` now guards against any remaining mixed ownership.
3. The plan uses checked `uint64` arithmetic throughout; an overflow is `CAPACITY_LIMIT` at problem creation.
4. `canon_result_encode_resume` never skips or duplicates: the offset advances by exactly the accepted count, a pause after partial acceptance keeps the prefix, a sink accepting more than offered is `OUTPUT_ERROR`.
5. The fault test asserts a `COMPLETE` result always carries its answer (bytes, witness, certificate when requested) and an incomplete one carries no flag, under every fault.
6. gcc and clang `-Werror` clean; ASan/UBSan clean (`make SANITIZE=1 test`, including the fault flow and the fuzz replay under ASan); `make check` and `ctest` (41 of 41 under `CANON_SANITIZE=ON`, 343 s) pass.
7. The checker's stream-backed size checks (the fuzz finding above) are shortcuts of rules the checker already applied later (`TRUNCATED`); they change no verdict on a certificate whose stream is long enough to be read, so no acceptance or mutation expectation moved.

## Left out

- Pool execution of batches (§14.1), batch cancellation, checkpoints (M6).
- The strict memory fallback of §11.2 (one mutable state, branch recipes) and worker-local arenas (M5).
- Per-stage times and hardware counters (M5 benchmark policy).
- A coverage-guided fuzzing run (needs a libFuzzer runtime; the harness is ready).
