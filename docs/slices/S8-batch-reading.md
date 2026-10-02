# S8 reading: the spec's batch semantics (`canon_solve_batch`)

**Status: reading; decided by the maintainer on 2 October 2026 (§5); no code.** `canon_solve_batch` stays a stub returning `CANON_UNSUPPORTED_ACTION` (`src/api/stubs.c`). This note records what `docs/specification.md` v2.0 pins about batch solving, what it leaves open, and what S8 must decide before implementing it.

## 1. Every sentence that mentions batches

Specification (normative):

- §17: "A workspace has one active owner; immutable contexts/registries/groups can be shared. `solve_batch` returns per-input statuses and preserves caller input order regardless of scheduling."
- §17: "`group_create`, `object_create`, `problem_create`, `workspace_create`, `solve`, `solve_batch`, `result_verify_witness`, `result_encode`, `checkpoint_write/read` specify their domain/action/mode arguments explicitly." and "Hot small-instance entry points use plain fixed-layout descriptors and preallocated workspace, with no callbacks/heap allocation after successful admission; the general API may use bounded stage callbacks."
- §14.1: "`CORE_POOL` owns a bounded pool for independent requests or a hard search. Never nest unrestricted pools. Batch parallelism is the default small-instance policy; coarse subtrees precede cooperative intra-node work for large instances."
- §12.3: "Reuse validated immutable groups, registries and workspaces; no per-call chain rebuild or allocator is required on an admitted fixed-capacity hot path." and "end-to-end throughput includes setup and drain."
- §12.1: "Add wrapper conversion, batch setup, allocator and collection costs to an end-to-end application boundary."
- §11.1 (applies to any admission, batch or not): "Requested managed-memory budgets use a deterministic admission plan: reserve a complete serial reference workspace plus bounded output/verification buffers [...]" and "Exceeding that policy yields `CAPACITY_LIMIT`, a function of semantic input, objective and descriptor, independent of worker count or lucky early discovery."
- §22: "Small batches use caller-owned workspaces; a bounded core pool serves hard instances. Strict capacity admission precedes optional acceleration."

Plans (process, not normative):

- Detailed plan §0a, slice table: S8 is "Capacity admission, live-memory ledger, metrics, fault injection, fuzz targets; `solve_batch`; output sink pause/fail".
- Detailed plan WP4.6: "`solve_batch` with order-preserving per-input statuses"; "Capacity admission precedes allocation (§11.1)."
- Detailed plan WP5.1 / WP5.4 and implementation plan M5: deterministic admission, the §11.2 ledger, "no allocation after admission on the hot path" for the small-batch regime.

## 2. What is pinned

1. One status per input (§17), from the §3.2 enum.
2. Results and statuses are in caller input order, independent of scheduling (§17).
3. Each problem's own `CAPACITY_LIMIT` is a function of its semantic input, objective and descriptor only (§11.1), so a batch cannot change a problem's capacity verdict by scheduling.
4. The workspace has one active owner (§17); a sequential batch on one workspace respects that.

## 3. What is open

1. **Overall return status.** No sentence says what `solve_batch` itself returns when some inputs fail: `COMPLETE` if the call ran (failures only in `statuses[]`), the first failing status in input order, or a severity-ordered maximum. §3.2 defines the enum but no aggregation.
2. **Continue or stop.** Nothing says whether an `INVALID_INPUT`, `CAPACITY_LIMIT` or `RESOURCE_LIMIT` on input `i` stops the batch or only marks `statuses[i]`; nor whether `CANCELLED` (§14.3) cancels the rest.
3. **Call-level versus per-input errors.** A NULL workspace, NULL arrays, `count` overflow, or a NULL entry in `problems[]`: whole-call `INVALID_INPUT` with no results, or per-input statuses?
4. **Admission and quota scope.** §11.1 admission "reserve[s] a complete serial reference workspace"; §17 forbids heap allocation "after successful admission" on hot entry points. Whether a batch is admitted once (one reservation covering every input, a shared budget) or per input, and whether a batch-level failure to admit is one status or `count` statuses, is not said. This is the §11.2 ledger S8 owns.
5. **Results on failure.** `canon_solve` returns a partial result for `CAPACITY_LIMIT` and sometimes `RESOURCE_LIMIT`; whether `results[i]` is then non-NULL in a batch, and the ownership of results already produced when the call fails at call level, is open.
6. **Execution mode.** §14.1 makes batch parallelism the default small-instance policy under `CORE_POOL`, while `CALLER_THREADS` forbids hidden threads. Which mode `solve_batch` runs under, and how a single `workspace` argument maps onto a pool, is M6/S8 territory.

## 4. Recommendation for S8

*Superseded by §5 (2 October 2026); kept as the record of what was recommended.*

- Decide items 1-3 in the S8 brief and send them as v2.1 spec items; the simplest reading consistent with §17 is: call-level `INVALID_INPUT` (no results written beyond NULL/`INVALID_INPUT` fill) only for NULL workspace/arrays or size overflow; otherwise every input is attempted in order, `statuses[i]`/`results[i]` exactly as `canon_solve` would give for that input alone, and the call returns `COMPLETE` iff it ran to the end (per-input failures live only in `statuses[]`).
- Pin byte identity: under `CALLER_THREADS` a batch must reproduce, input by input, the status, flags, trace, bytes and witness of separate `canon_solve` calls (PROFILE-EQUIV); test this on T1 for every objective, plus a batch of 0 and a batch with one invalid input.
- Decide admission scope (item 4) together with the ledger: per-input admission keeps §11.1's per-problem determinism trivially; a batch-wide reservation must still yield per-input verdicts independent of order.
- Keep cancellation (item 2) and pool execution (item 6) out of the first `solve_batch`; land the sequential path first, with the pool behind the same contract in M6.

## 5. Decided (2 October 2026)

The maintainer's decision (D12) settles §3 items 1, 3, 4 (admission and quota scope), 5 and 6, and the non-cancellation part of 2; whether `CANCELLED` (§14.3) on one input cancels the rest is not decided. The contract now lives in `docs/specification.md` §17 v2.1, which is normative; this section restates it and supersedes §4.

1. **Call-level rejection.** `canon_solve_batch` rejects the whole batch with `INVALID_INPUT`, writing nothing, only for a NULL workspace, NULL `problems`/`results`/`statuses` arrays with `count > 0`, or a `count` whose byte size overflows.
2. **Per-input attempt.** Otherwise every input is attempted in caller order, and `statuses[i]`/`results[i]` are exactly what `canon_solve` would return for that input alone. A NULL entry in `problems[]` gives `INVALID_INPUT` for that entry only.
3. **Return status.** The call returns `COMPLETE` iff every input was attempted; per-input failures appear only in `statuses[]`.
4. **Admission and quota.** Capacity admission and the quota are per input.
5. **Execution mode.** Under `CALLER_THREADS` the batch runs sequentially on the one workspace; pool execution (§14.1) must reproduce the same statuses and bytes.

Tests S8 must carry:

- A batch of 0 inputs.
- Byte identity with separate `canon_solve` calls on every T1 objective (status, flags, trace, bytes and witness, input by input).
- One invalid input among valid ones: only its status is `INVALID_INPUT`; the others are unaffected and the call returns `COMPLETE`.
- A NULL entry in `problems[]`: `INVALID_INPUT` for that entry only.
- A NULL array with `count > 0`: rejected at call level with `INVALID_INPUT`, nothing written.
- Per-input `CAPACITY_LIMIT`: a tiny quota on one input only gives `CAPACITY_LIMIT` for that input, with every other input's status and bytes unchanged.
