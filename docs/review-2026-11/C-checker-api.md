# Review lane C-checker-api (November 2026)

## 1. Scope

Read in full or in relevant part: `docs/review-2026-11/BRIEF.md`, `README.md`,
`HANDOFF.md`, `checker/README.md`, all six checker sources (`checker/cc.h`,
`cc_reader.c`, `cc_chain.c`, `cc_object.c`, `cc_refine.c`, `cc_check.c`,
`main.c`), `include/canon/canon.h`, `src/api/api.c`, `src/api/stubs.c`,
`tools/canon-cli.c`, `src/search/certificate.h`, `docs/specification.md` (v2.2,
whole), `docs/pruning-rules.md`, `docs/detailed-implementation-plan.md`,
`Makefile`/`CMakeLists.txt` checker rules, `refs/ref-b/QUESTIONS.md` (as spec
questions only; nothing in `refs/` was run), `fuzz/fuzz_checker.c` header.

Per `docs/review-2026-11/BRIEF.md` no test suites, reference runs, corpus,
fuzz replays or benchmarks were run. **No evidence in this report was run**;
every finding is reasoned from the sources quoted, with `path:line`. Counter-
examples are derivations, not executions. This report was written to disk
before the remaining producer-side cross-checks were finished; items still
open are marked **IN PROGRESS** at the end.

Not covered: producer internals except where cited (`src/refine/p1.c`
signature layout and `src/search/p1_tree.c` quota/shortcut counting are
IN PROGRESS), `src/bsgs/verify.c` body (only the checker's own verifier was
audited), `checker/tests/test_checker.py` body beyond `checker/README.md`'s
description, the membership/action bodies behind `src/search/objectives.c`
(callees of `canon_result_verify_witness`), Lean, benchmarks, refs beyond
`refs/ref-b/QUESTIONS.md`.

## 2. Findings (most severe first)

### C-checker-api-01 — Pruned runs keep profile `0x0001` although the ANY-mode witness need not be P1's witness; spec §11.1 states the opposite condition

- Severity: **high**. Category: spec-conformance / soundness (of the witness claim).
- **Evidence** (reasoned, not run). Spec §11.1 (line 367): "The profile ID
  names the output semantics only (a pruned run that reproduces P1's trace,
  bytes **and witness** reports profile `0x0001`)". Spec §7.4 preamble (line
  246): "elsewhere minimise among the witnesses that actually attain that
  key" — over the *unpruned* tree's leaves. But `docs/pruning-rules.md:42`
  states that under `witness_mode = ANY` a pruned run "returns the least `t`
  among the **explored** leaves attaining the key, a valid witness … that may
  differ from the unpruned run's", and `include/canon/canon.h:527-529`
  repeats this for `PRUNE_1`/`PRUNE_2` (the default). The producer's
  certificate does the same: `src/search/certificate.h:124-127` defines the
  certified witness as "the least explored-leaf witness t attaining it",
  and the checker computes exactly that minimum over explored leaves
  (`checker/cc_check.c:250-297` `leaf()`, tie-break at 280-282; final compare
  633-638). So under the default policy `0x0003`, a certificate can be
  ACCEPTed (header profile `0x0001`, `checker/cc_check.c:491-496`) whose
  stated witness is **not** the §7.4 witness and not necessarily the
  §3 deterministic witness, yet `checker/README.md:13` says acceptance means
  the claim holds "with the stated witness", and the result's evidence mode
  becomes `CHECKED_CERTIFICATE` (`src/api/api.c:1433-1439`) although the
  *result's own* deterministic witness — computed after P1 in
  `src/api/api.c:1181-1186` — is never inside or covered by the certificate.
- **Recommendation.** Decide at spec level: either (a) §11.1's parenthetical
  is amended so a pruned run keeping profile `0x0001` must reproduce only
  trace and bytes, with the witness explicitly re-scoped to "least explored-
  leaf witness" in §7.4/§3.2 and in `checker/README.md`'s acceptance
  sentence; or (b) pruned certificates carry and the checker verifies the
  §3 deterministic witness (certificate v1), and ANY-mode pruned results do
  not claim the §7.4 witness. Until then, `canon_result_evidence` should not
  be describable as certifying the result's witness field in deterministic
  mode.
- **Remediation plan.** Spec edit (maintainer) to §7.4/§11.1/§3.2; align
  `checker/README.md:13,62,68`, `include/canon/canon.h:516-541` witness
  docs, `src/search/certificate.h:124-127`. Tests that would prove the fix:
  a mutation replacing a certificate's final witness with a *smaller*
  attaining witness from a pruned leaf must be rejected or accepted exactly
  as the amended text says; an API test asserting what
  `CANON_EVIDENCE_CHECKED_CERTIFICATE` does and does not cover in
  deterministic mode. Risk: low (documentation/semantics pinning); effort:
  small for (a), medium for (b).

### C-checker-api-02 — Work-policy node counts (hence `CAPACITY_LIMIT`) depend on the generator presentation, not only on the semantic input

- Severity: **high**. Category: spec-conformance (representation independence, §5/§11.1).
- **Evidence** (reasoned counterexample, not run). Spec §5 (lines 164-166)
  requires every semantic stage to factor through `decode` and excludes
  "SGS/base choice, generator order … from mathematical choices"; §11.1
  (line 365) makes `CAPACITY_LIMIT` "a function of semantic input, objective
  and descriptor", and line 367 repeats "The count is a function of input,
  objective and descriptor". But the only automorphisms available to
  policy `0x0002` are "the input generators of `G` that fix `x`"
  (`docs/pruning-rules.md:34`, source (i)), and line 55 concedes the count is
  determined by "`A_known` (a function of the generators **as given** and
  `x`)". Derivation: `G = Sym(3)`, `x = {0}`, `A = ⟨(1 2)⟩`. Generators
  `{(0 1 2), (0 1)}`: neither fixes `x`, so `A_known = 1`. Generators
  `{(0 1 2), (1 2)}`: `(1 2)` fixes `x`, so `A_known = A`. P1's root
  partition is `[{1,2},{0}]`; the G stage does not split `{1,2}` (one orbit
  of the stabiliser of `0` in `Sym(3)`), so the root branches on cell
  `{1,2}` with two children, each a leaf. Policy `0x0002` explores both
  children under the first presentation (3 nodes) and prunes one under the
  second (2 nodes): identical group, object, objective and descriptor,
  different quota count — and different status at a quota of 2. Adding the
  redundant generator `(1 2)` alone changes the count, against §5's intent.
  The same class of defect is already recorded for the admission plan
  (HANDOFF open item 6(i); see C-03), but for the *quota count* it is not
  listed as open there.
- **Recommendation.** Either define "input" in §11.1 to include the exact
  generator list (weakening §5 for statuses, and say so in §5), or make
  source (i) presentation-independent for counting purposes (e.g. count the
  traversal the policy names over a function of `⟨generators⟩` only), or
  restrict the determinism claim to fixed presentations in
  `docs/pruning-rules.md` and §11.1.
- **Remediation plan.** Maintainer decision first (spec §5/§11.1). If
  presentation-independence is kept: change source (i) seeding in
  `src/symmetry/` to a presentation-independent subgroup (costly) or define
  the counted traversal without generator-seeded pruning. Tests: the Sym(3)
  derivation above as a C test under both presentations at quotas 2 and 3,
  policies `0x0002`/`0x0003`; generator-redundancy metamorphic cases in the
  prune tier. Risk: policy/count changes ripple into recorded node counts;
  effort: small to document, medium-large to make the count invariant.

### C-checker-api-03 — Admission (`max_live_bytes`) verdicts depend on backend and on the chain built from the given generators; the plan is a calibrated estimate, not the §11.1/§11.2 bound

- Severity: **high** (already partly recorded as HANDOFF open item 6(i); the
  estimate-vs-bound part goes beyond it). Category: spec-conformance / contract.
- **Evidence** (reasoned). `src/api/api.c:582-589` `group_bytes` returns
  `canon_bsgs_bytes(chain)` for the chain backend — a measured size of the
  chain built from the supplied generators — or `|G|·degree·2·4` for the
  explicit backend; `plan_bytes` (`api.c:670-731`) charges `4×` that value
  (line 708) plus a `64·(log2|G|+1)` dense-row term whose header comment
  (`api.c:601-606`) says it is "an estimate calibrated on the tiers and
  checked by tests/c/test_metrics.c, **not a proof**: the strong generator
  count of a subgroup chain has no useful closed bound". The problem is
  then refused with `CAPACITY_LIMIT` when `planned > max_live_bytes`
  (`api.c:893-901`). Two generating sets of one group, or the two backends
  (a context option, `canon.h:202-205`), therefore give different verdicts
  at a budget between their plans — while spec §11.1 (line 365) requires
  `CAPACITY_LIMIT` to be "a function of semantic input, objective and
  descriptor, independent of worker count", and §11.2 (line 385) requires
  that "Deterministic size planning must bound their largest scratch
  state". A calibrated estimate that is not a bound also means actual live
  bytes can exceed the plan; no status then depends on the measurement
  (HANDOFF D16), so the failure surfaces at best as schedule/OS-dependent
  `RESOURCE_LIMIT`, outside the admission contract. Group and object
  *creation* are not subject to `max_live_bytes` at all (only problem
  creation is), so the largest single allocations (an explicit table, a
  chain) are already made before the budget is consulted.
- **Recommendation.** Settle the HANDOFF 6(i) question in the spec: name the
  generator list and backend as plan inputs, or make the plan a function of
  the semantic group. Replace the calibrated chain term with a proven bound
  or rename the admission claim in `canon.h:158-163` from a plan "policy"
  to an estimate with explicit non-guarantee.
- **Remediation plan.** Spec §11.1/§11.2 decision; then `plan_bytes` terms
  in `src/api/api.c`, docs in `include/canon/canon.h:158-163,186-192`.
  Tests: same group, two presentations and both backends, budgets straddling
  the two plans, asserting whatever the decided rule is; a stress case
  targeted at the `64·(log2|G|+1)` term seeking `peak > plan` beyond the
  current tiers. Risk: planned-byte values are metric-visible; effort:
  medium.

### C-checker-api-04 — The checker does not certify the named work policy's traversal, and the quota is not in the certificate at all

- Severity: **medium**. Category: soundness (scope of the accepted claim) / spec-conformance.
- **Evidence** (reasoned). `checker/README.md:62` rule 1 and line 68 rule 7:
  the checker "does not re-derive which pruning policy produced a pruned
  tree"; under `0x0001` it checks only that nothing was pruned
  (`checker/cc_check.c:646-648`); `0x0002` and `0x0003` are interchangeable
  for any correctly justified pruned tree (`docs/pruning-rules.md:65`). Any
  `a ∈ Aut_G(x)` listed in the automorphism table justifies a prune (rules
  2 and 5, `cc_check.c:572-601,365-399`) regardless of whether the named
  policy could have discovered it when the decision was taken. The image
  claim remains sound (the pruning lemma needs only one such `a`), but the
  accepted `U64(nodes explored)` is then **not** certified to be the count
  of the header's work policy (spec §11.1, line 367: the policy "is
  recorded in results and certificates beside the profile" precisely so the
  count is checkable), so an ACCEPT cannot support a quota/`CAPACITY_LIMIT`
  claim under `0x0002`. Separately, the CERT-0 layout (`src/search/
  certificate.h:12-19`; restated `checker/README.md:36-42`) contains no
  capacity descriptor and no quota value at all, while the detailed plan's
  WP4.7 (`docs/detailed-implementation-plan.md:346`) specifies certificate
  v0 to include "the capacity descriptor". Spec §20 (line 533) requires
  producer certificates to include "coverage, group verification,
  branch/bound/symmetry rules and final objective claims".
- **Recommendation.** State the accepted claim's exact scope in
  `checker/README.md` ("image, trace, explored-tree minimum; not policy,
  not quota"), or add the descriptor/quota and the policy legality checks
  (automorphism source and discovery time) to certificate v1.
- **Remediation plan.** Docs first (`checker/README.md`, `HANDOFF.md` S7
  decision 3/S7b decision 3 already record the choice); v1 format work in
  `src/search/certificate.h`/`.c` and `checker/cc_check.c` if policy
  certification is wanted. Tests: relabel a `0x0003`-justified certificate
  as `0x0002` (currently ACCEPT) — pin the intended verdict once decided.
  Risk: low; effort: small (docs) to medium (v1).

### C-checker-api-05 — The checker's idea of a legal object stream is weaker than spec §4.1/§4.2 (non-canonical, partially unreachable streams decode to an object)

- Severity: **medium**. Category: spec-conformance (not a false-image hole).
- **Evidence** (reasoned). Spec §4.1 (line 115): for a complete stream
  "**all records are reachable from root**"; §4.2 (line 140) makes
  "unreachable records … invalid canonical encodings" and the root "the
  last record". `checker/cc_object.c:198-397` `cc_object_decode` enforces
  none of: root-is-last (only `root < q`, lines 301-305), reachability
  (extra well-formed records in tags `01/04/09` are parsed and ignored),
  canonical DAG numbering/height order (§4.2 lines 140-142), or graph arc
  sort order/no-duplicates (it normalises, lines 172-190, 382). It does
  enforce child references `< j` and increasing (252-258), atoms `< n`
  (238-240), arc endpoints/multiplicity (288-291), shortest `Nat`
  (146-151), and no trailing bytes (306-308). Because the checker
  re-encodes leaf images itself (`cc_object.c:503-539`) and derives the
  object extensionally, an accepted certificate still proves the canonical
  image *of that extensional object*; but it accepts certificates whose
  `B(stream of x)` is not a legal complete/canonical CDAG-2 stream, so two
  different byte streams can both be "the" input of an accepted
  certificate for one object — the input's identity as persistent key
  (§4.3, line 150) is not what was checked. Honest producer streams are
  canonical; this is an adversarial-certificate scope gap.
- **Recommendation.** Either enforce canonical form on the embedded stream
  (decode → normalise → re-encode → byte-identity, the §4.2 validator
  rule) before rule 3, or state in `checker/README.md` that acceptance is
  relative to the extensional decode of a merely well-formed stream.
- **Remediation plan.** `checker/cc_object.c` + `cc_check.c:513`: compare
  the embedded stream with `cc_object_encode(x)` and reject on mismatch
  (new reason or reuse `OBJECT_MALFORMED`); mutations in
  `checker/tests/test_checker.py` for an unreachable record, a permuted
  record order and a duplicated arc in the *input* stream. Risk: low
  (producer output is canonical); effort: small.

### C-checker-api-06 — Checker resource behaviour on hostile input: unbounded total memory and super-quadratic work; `CAPACITY` is reported as a rejection

- Severity: **medium**. Category: memory-safety (robustness/liveness on adversarial input), contract.
- **Evidence** (reasoned). (a) `CC_MAX_ALLOC` caps each *single* allocation
  at 1 GiB (`checker/cc.h:24-28`, `cc_reader.c:57-75`), not the total: one
  verified chain simultaneously holds generator rows, inverse rows, inputs
  and the `p × n` provenance evaluation (`cc_chain.c:462-523`), and each
  chain level allocates an `n`-word `pos` array (`cc_chain.c:81-84`) so
  `depth × n` words (quadratic in `n`, while the stream backing is only
  O(depth) small fields per level) can be demanded from a small file.
  `checker/main.c:16-49` also reads the whole input with unbounded
  doubling. Failure mode is `CC_OUT_OF_MEMORY`/exit 2 or OOM-kill, not a
  false accept — but it is not the bounded behaviour the README's
  "checked allocation" summary suggests. (b) Worst-case work is far above
  certificate size: nesting is `O(depth · s_i · s_{i+1} · n)` byte
  comparisons (`cc_chain.c:333-351`) with `s` bounded only by backed stream
  words and repeatable generator ids; refinement splits are insertion
  sorts per cell per sweep (`cc_refine.c:57-68`) with full signature-vector
  compares; every sweep of every node re-verifies a whole chain. A crafted
  certificate that fails late (or never) can keep the checker busy for
  orders of magnitude longer than its size justifies. (c) `checker/cc.h:25`
  says a `CC_CAPACITY` outcome is "never a verdict on the certificate",
  but `checker/main.c:74-83` prints `REJECT CAPACITY` with exit 1 (only
  `OUT_OF_MEMORY` gets exit 2): a valid-but-huge certificate is reported
  as rejected, contradicting the documented three-way protocol in
  `checker/README.md:13`.
- **Recommendation.** Add a total-bytes budget (charge every `cc_alloc`
  against one ledger, as the library does) and cheap structural caps
  (e.g. total level generator references, `depth` already `≤ n`); route
  `CC_CAPACITY` to exit 2 with a "cannot decide" message.
- **Remediation plan.** `checker/cc_reader.c` (ledger in `cc_alloc`),
  `checker/main.c:74-83`, README rule 7. Tests: oversized-`p` and
  many-level chains must exit 2 quickly; an `s`-inflated nesting case as a
  timeout regression. Risk: low; effort: small-medium.

### C-checker-api-07 — "Written from the specification only" overstates the checker's independence: CERT-0 is a producer-side format, restated from `src/search/certificate.h`, by (per HANDOFF) the same single-agent session

- Severity: **medium**. Category: soundness (assurance claim) / documentation.
- **Evidence** (reasoned). Code separation itself is real and
  build-enforced: every checker file includes only `<stdbool/stddef/
  stdint/stdlib/string/stdio>` and `"cc.h"` (grep of `checker/`); the
  Makefile rule compiles `CHECKER_SRC` with no `-Iinclude`/`-Isrc` and no
  library, CMake links only the `canon_options` interface (compiler flags,
  `CMakeLists.txt:52-54`). But the *format* is not in the spec:
  `checker/README.md:32` heads the format section "restated from
  `src/search/certificate.h`", and line 5 concedes "the S3 verifier
  `src/bsgs/verify.c` is a reference text for the list of conditions".
  Spec §20 (lines 525-535) requires a checker and lists what certificates
  must include, but nowhere defines CERT-0's layout, the CHAIN layout, or
  provenance kinds `1/2/3` — those are shared design artefacts (slice S7
  brief + producer header). HANDOFF records S7/S7b were implemented by "a
  single agent … ('work alone')" with self-reviews (HANDOFF line 3).
  `checker/cc.h:3-4` nevertheless claims the program "shares no code,
  header, type **or constant** with the canon library" — format constants
  (objective/profile/encoding IDs, trace token bytes, provenance kinds)
  are necessarily common, and a shared *misreading of the spec* in the S7
  design (e.g. the O-signature layout, C-ambiguity §3.6 below; the
  G-stage minimality characterisation, `cc_check.c:221-228`) would be
  common-mode between producer and checker even though no code is shared.
- **Recommendation.** Reword the independence claims (`cc.h`, READMEs) to
  "no shared code; format and condition list taken from the S7 brief and
  producer header"; consider promoting CERT-0/CERT-1 into the spec (or a
  frozen format document owned independently of `src/`) before M4, so a
  future checker can be written from normative text alone.
- **Remediation plan.** Documentation edits; optionally move the format
  definition into `docs/specification.md` §20 or a new frozen doc and make
  `src/search/certificate.h` cite it instead of the reverse. Tests: none
  mechanical; a grep guard in CI over `checker/` includes would pin the
  code-level claim. Risk: none; effort: small.

### C-checker-api-08 — Public header contradicts itself and the implementation on deterministic witnesses, and is stale on evidence mode and object kinds

- Severity: **medium**. Category: contract / documentation.
- **Evidence** (reasoned). (a) `include/canon/canon.h:393-405` defines
  `CANON_WITNESS_DETERMINISTIC` for, and "Accepted for every objective"
  including `TRANSPORTER_ONE`, labeling and signed; the supported-
  combination table at 441-451 still restricts labeling/signed to
  "witness mode CANON_WITNESS_ANY", and the validation order at 458-462
  says deterministic mode for `TRANSPORTER_ONE`, `STABILISER`,
  `CANONICAL_LABELING_COSET` or `SIGNED_CANONICAL_IMAGE` is
  `CANON_UNSUPPORTED_ACTION`. The implementation follows the first text:
  `combination_supported` ignores witness mode (`src/api/api.c:529-557`),
  mode is validated only as an enum value (758-761), and deterministic
  witnesses are implemented for those objectives (api.c:1181-1186,
  1211-1264). Header comments at 451-462 are the stale S4 reading that
  M0a withdrew (HANDOFF M0a decision 1). (b) `canon_solve`'s doc
  (`canon.h:486-506`) ends "Evidence mode: TRUSTED_ENGINE", but a solve
  with `options->certificate` yields evidence `CHECKED_CERTIFICATE`
  (`canon.h:428-436`, `api.c:1433-1439`). (c) `canon_problem_create`'s doc
  (370-387) says the object "may be a subset or a graph (S2)", omitting
  nested (S5) roots the function accepts. (d) `canon.h:4` cites spec
  "v2.1" as normative while `docs/specification.md:3` is v2.2 (also
  `docs/pruning-rules.md:3` "v2.1", `docs/detailed-implementation-
  plan.md:3` "aligned with specification v2.0").
- **Recommendation.** One editorial pass over `canon.h` against spec v2.2
  and the M0a/S7 decisions; treat header validation orders as the frozen
  client contract only after this pass (the header itself says
  provisional until M4, line 29-30).
- **Remediation plan.** Edits confined to comments in
  `include/canon/canon.h` (lines 4, 372, 441-472, 506). Tests:
  `test_header_abi`-style doc/contract tests are not feasible; instead a
  C test asserting deterministic mode is accepted for every supported
  objective (already implied by tiers) pins the corrected text. Risk:
  none (comments); effort: small.

### C-checker-api-09 — Spec-level: quota counting is under-determined at exactly the boundaries where statuses must agree (ref-b Q1–Q3, Q10)

- Severity: **medium**. Category: spec-conformance (ambiguity with status-visible consequences). See also §3 below; promoted to a finding because §20 requires status agreement "under fixed capacity".
- **Evidence** (quoted; reasoned). §11.1 line 367: the quota counts "P1
  `NODE` tokens plus §8.1 enumerator visits; the §7.3 root shortcuts …
  visit no tree nodes"; line 365: "Exceeding that policy yields
  `CAPACITY_LIMIT`". Three readings remain open in the text and are
  recorded verbatim in `refs/ref-b/QUESTIONS.md` Q1 (count `>` quota vs
  `≥` quota fails), Q2 (a "visit" = every `visit(H,r)` call incl. internal
  regions — `1+4+12+24=41` for `Sym(4)` — vs only `consume` leaves), Q3
  (both §7.3 shortcuts count 0 vs only the singleton-orbit one does; the
  discrete-root shortcut still emits a `NODE(0)` trace token per §7.4 line
  250, and §7.1 line 213 "One complete no-change sweep is always recorded,
  even at a discrete root"), Q10 (on the singleton-orbit route the
  deterministic-mode stabiliser enumeration is still counted in full for
  unsigned objectives, while §3 line 80 exempts only the signed route).
  HANDOFF open item 4 records these "can only show on quota-boundary
  cases, which T3 is the first to exercise" and that the question review
  "is not done".
- **Recommendation.** Pin all four in §11.1 before the T3 comparison is
  treated as M0 evidence; add quota-boundary cases (quota `= count−1, =
  count, = count+1`) to the corpus schema's tiers.
- **Remediation plan.** Spec §11.1 editorial (no golden bytes change);
  corpus generator gains boundary cases; both refs re-run on those only.
  Risk: recorded counts in metrics may shift by the discrete-root unit;
  effort: small.

### C-checker-api-10 — Sink/encode contract gaps: result lifetime during the callback is unstated; the spec's "final commit marker/length" has no API surface

- Severity: **low**. Category: contract / memory-safety (client misuse surface).
- **Evidence** (reasoned). `canon_result_encode_resume`
  (`src/api/api.c:1555-1587`) caches `bytes`/`total` once and reads
  `bytes + *offset` after each sink call; the result is not retained
  across the callback, and nothing in `canon.h:110-118,649-667` tells the
  client it must not release the result (its last reference) from inside
  the sink — doing so frees `result->bytes` (`api.c:271-287`) and the next
  chunk read is use-after-free. (Retaining the buffer beyond the callback
  *is* forbidden, `canon.h:110`.) Spec §17 (line 501) additionally
  requires "A final commit marker/length … before a consumer treats a
  stream as a complete key"; the API provides neither a marker nor a
  total length from `canon_result_encode` (only the resume offset, which
  `canon_result_encode` discards internally, `api.c:1589-1593`). The
  completion *status* may be intended as the commit signal, but the spec
  sentence is not mapped to it anywhere.
- **Recommendation.** Document "the result must outlive the encode call;
  releasing it inside the sink is invalid" (or retain internally across
  the loop — one atomic retain/release per encode), and either map §17's
  marker/length to the `COMPLETE` return plus final offset or add a
  length accessor use.
- **Remediation plan.** Comment in `canon.h` + optionally
  `canon_result_retain` bracketing in `canon_result_encode_resume`
  (`const` handle prevents the public retain; use the internal refcount).
  Test: a sink that releases its only extra reference mid-stream must
  still deliver the remaining bytes (if retain fix chosen). Risk: low;
  effort: small.

### C-checker-api-11 — Metrics peak is process-global and reset per solve, so the documented per-solve meaning fails for the documented concurrent use

- Severity: **low**. Category: contract.
- **Evidence** (reasoned). The header permits sharing problems/results
  across threads with per-thread workspaces (`canon.h:220-227`), while
  `canon_metrics.live_bytes_before/peak_live_bytes` are described as
  "process-wide … during the solve" (`canon.h:616-617`) and are produced
  by a process-wide `canon_mem_peak_reset()` at every solve start
  (`src/api/api.c:1140-1153`). Two concurrent solves therefore reset and
  pollute each other's peaks; HANDOFF D17 concedes exactness only "when
  one solve runs at a time", a qualification absent from `canon.h`.
  Statuses never depend on these numbers, so the damage is limited to
  metrics truthfulness (spec §19 line 521 lists "live memory by §11"
  among reported metrics).
- **Recommendation.** Copy the D17 qualification into `canon.h` (and note
  `canon_memory_get`'s peak is reset by any solve, lines 169-175), or move
  to per-solve deltas of a monotonic counter.
- **Remediation plan.** Header comment; test: two interleaved solves
  document (not assert) peak behaviour. Risk: none; effort: trivial.

### C-checker-api-12 — Checker/API surface left at stub or absent while spec text describes it as first-delivery

- Severity: **low** (all consciously recorded in HANDOFF; listed so the
  spec/plan/code triangle is explicit). Category: spec-conformance /
  documentation.
- **Evidence** (reasoned). (a) Checkpoints: spec §18 line 509 "First
  delivery supports **trusted engine resumptions** only", but both entry
  points are permanent `UNSUPPORTED_ACTION` stubs (`src/api/stubs.c:12-32`)
  and HANDOFF defers them to M6; there is also **no
  `canon_checkpoint_release`** prototype anywhere in `canon.h`, although
  §17 lists checkpoints among "Opaque retain/release handles" and
  `canon.h:220-221` promises release of any failed/partial handle — the
  stub sets `*out = NULL`, masking the gap today. `canon_registry`
  (`canon.h:102`) has no functions at all. (b) `CONSTRAINT_ONE/ENUM` are
  refused at problem creation (`api.c:554-556`), consistent with §2.1's
  recognised-but-unavailable rule, but §17's adapter/predicate
  registration surface does not exist in the header, so the objectives are
  inexpressible, not merely unimplemented. (c) §17 line 497 "no
  callbacks/heap allocation after successful admission" for hot entry
  points: the header designates no hot entry point and solves allocate
  after admission (`RESOURCE_LIMIT` paths throughout `finish_result`,
  `api.c:1002-1088`); the detailed plan defers this to M5 (plan line 376).
  (d) The toolchain-strict header claims argument lists "PROVISIONAL
  until M4" (`canon.h:29`), while spec §17 line 503 makes client-level
  error/lifetime vectors a release precondition — fine as sequencing, but
  the spec's present-tense API descriptions (§17) should not be read as
  the current contract.
- **Recommendation.** Add the missing `canon_checkpoint_release`
  declaration now (no-op-valid on NULL) so the frozen §3.2/§17 lifetime
  promise is expressible; keep the rest as tracked deferrals.
- **Remediation plan.** Header + stub for checkpoint release; one line in
  HANDOFF open items tying §18's "first delivery" to M6. Risk: none;
  effort: trivial-small.

### C-checker-api-13 — CLI: cost and expressiveness rough edges around the public API

- Severity: **low / note**. Category: contract (tool, not library).
- **Evidence** (reasoned). (a) Generator storage `gen_count × n` words is
  allocated before any generator list is parsed and before the library's
  degree check (`tools/canon-cli.c:740-760`): `gen_count` is derived from
  `;` count in `--gens`, so a short argument with huge `--n` requests a
  multi-gigabyte `malloc` first (overflow itself is checked at 741-743).
  Local-user input only; still, `--rho` (765-770) and `--colours`
  (221-224) already demonstrate the bounded-by-argument pattern.
  (b) Object and target are forced to one `--kind` (`build_object` called
  with `o.kind` for both, 813-819; mixing option families is a usage
  error, 524-528), so cross-kind transporter cases — whose semantics
  changed in M0a (empty result) — are expressible through the CLI only
  via two `--stream`s; `canon_object_create_simple_graph`,
  `canon_group_order`, `canon_group_character`, `canon_solve_batch`,
  metrics and encode-resume have no CLI surface at all (API-only
  coverage). (c) The record is printed before the certificate file is
  written (834-839 + header comment 58-60): on write failure the exit
  code is 3 but a complete-looking `COMPLETE` record is already on
  stdout — documented, but consumers must check the exit code, which
  test harnesses should assert explicitly.
- **Recommendation.** Bound the generator allocation by parsed counts
  (parse into growable storage or validate list lengths first); leave the
  rest as documented tool limits.
- **Remediation plan.** `tools/canon-cli.c` main generator loop. Tests:
  `--n 100000000 --gens ";"` must be a usage error without a large
  allocation. Risk: none; effort: small.

## 3. Spec ambiguities (quoted; not resolved here)

1. **§11.1 (line 367)** "A logical work quota … counts the nodes of the
   traversal …: P1 `NODE` tokens plus §8.1 enumerator visits; the §7.3
   root shortcuts are part of profile P1's policy and visit no tree
   nodes." — Whether "visit" includes internal regions; whether the
   discrete-root shortcut (which emits `NODE(0)`) counts; whether
   "Exceeding" (line 365) means `>` or `≥`. (Finding C-09; ref-b Q1–Q3,
   Q10.)
2. **§11.1 (line 367)** "(a pruned run that reproduces P1's trace, bytes
   and witness reports profile `0x0001`)" vs the ANY-mode explored-leaf
   witness of S7/S7b. (Finding C-01.)
3. **§11.1 (line 365)** "Exceeding that policy yields `CAPACITY_LIMIT`, a
   function of semantic input, objective and descriptor" — whether the
   generator list, the backend, and a measured chain are part of
   "semantic input" (HANDOFF open item 6(i); findings C-02, C-03).
4. **§4.4 (line 156)** "`SIMPLE-UPPER-1` … is available only for
   uncoloured simple undirected graphs" — no status is named for an
   ineligible object; §2.1 line 42's "A recognised but unavailable type
   returns `UNSUPPORTED_ACTION`" is about types. Implemented and ref-b
   (Q6) both read `UNSUPPORTED_ACTION`; HANDOFF open item 6(ii) records
   it as undecided in the spec.
5. **§4.2 (line 138)** "Validate acyclicity … before interning. Source
   sharing, unreachable allocation records and insertion order have no
   meaning. Discard unreachable input nodes" — whether a *malformed*
   unreachable record invalidates an import (ref-b Q4, reading A chosen;
   the checker's decoder also parses every record, `cc_object.c:231-300`,
   but then *accepts* well-formed unreachable ones, C-05).
6. **§7.1 (lines 191-194)** "sig(v) consists of exact counts, in order:
   for arc labels sorted by B(label), then cell index j: outgoing
   multiplicity from v into C[j], incoming multiplicity from C[j] into v"
   — the (label, j) pair layout `(out,in,out,in,…)` vs per-label
   out-block then in-block (ref-b Q8; the checker implements the pair
   layout, `cc_refine.c:132-135,154-157`). The §7.4 arc case (one label,
   one cell) cannot distinguish them, and the split-class order — hence
   traces — can differ.
7. **§7.1 (line 182)** "On the top-level subset (a set consisting only of
   atom nodes) … on a top-level graph …" — determined by the normalised
   value or by the input kind (ref-b Q7; API and checker both use the
   normalised root, `canon.h:351-353`, `cc_object.c:311,337`).
8. **§3.2 (line 96)** the eight statuses are listed with no precedence
   rule when several failures coincide (ref-b Q9). The C header documents
   elaborate per-function orders (`canon.h` passim) that the spec never
   states; §20 status agreement between blind references needs one.
9. **§7.3 (line 242)** "If every generator fixes x, the unsigned orbit is
   a singleton and returning x is equivalent to P1 regardless of trace.
   A caller requesting a trace certificate receives the prescribed trace
   or a checker rule for this shortcut." — CERT-0/checker have no
   shortcut rule (full tree always); which trace a non-certificate
   shortcut result would report through the API is unstated. IN PROGRESS:
   whether the producer ever takes this shortcut outside metrics.
10. **§17 (line 501)** "A final commit marker/length is required before a
    consumer treats a stream as a complete key" and "Sink failure may
    leave `image_canonical=true` and `encoding_complete=false`" — neither
    maps onto the current encode API (no marker/length; `encoding_
    complete` is set at solve and never modified by encode,
    `api.c:1555-1593` "The result is never modified"). IN PROGRESS: is
    "may" permissive enough to cover the implementation, or is a marker
    byte intended on the wire?
11. **§9.2 (line 330)** "Group order is an exact multi-limb product ∏r_i;
    it is not uint64 in general" and **§11.1 (line 363)** "exact
    multi-limb counts/orders/multiplicities", vs the shipped `uint64`
    order with `CAPACITY_LIMIT` at `|G| ≥ 2^64` (`canon.h:248-249`,
    HANDOFF decision 2) and the detailed plan §2.1 decision
    `max_count_bits = 64` "which §11.1 permits" — the spec sentence that
    permits a 64-bit cap is not identified; §2.1 line 42 makes
    mathematical coverage "subject to the finite machine capacities in
    §11", which may or may not be that permission.
12. **§20 (line 527)** "require **two blind independently written
    reference implementations**" vs HANDOFF (2 October decisions, item 2)
    reading it as "two independently written" with ref-a "recorded as not
    blind with respect to `checks/` and `tools/`"; the spec text was not
    amended. Related staleness: `CLAUDE.md` and HANDOFF line 3 still say
    ref-b "is not written and needs an author", while `README.md:5` and
    HANDOFF open item 4 record ref-b sealed 19 October 2026.
13. **Detailed plan WP4.5 (line 338)** states the first prune with an
    automorphism that "fixes the current node's individualised sequence
    **setwise**"; the correct (implemented, checker-enforced) hypothesis
    is pointwise (`docs/pruning-rules.md:17-19` lemma; `cc_check.c:380-385`).
    Setwise-only fixation does not give `S^a = S` as a sequence, so the
    plan sentence as written would license an unsound prune; the plan is
    not normative (its line 5), but it is the M4 work statement.
14. **§3.1 (lines 90-92)** signed labeling orientation `σ_ρ`: "an
    implementation may return `UNSUPPORTED_ACTION` for a signed labeling
    problem". The public API cannot express a signed labeling problem at
    all (no `σ_ρ` parameter; labeling on a signed group ignores signs,
    `canon.h:456-457`), so it is undefined which input, if any, the
    permission applies to, and how the M0 case schema encodes one (cf.
    ref-b Q9's precedence inventions).

## 4. Things checked and found sound

- **Checker chain verifier** (`cc_chain.c:227-523`): structure → bijections
  (own inverses only after this) → provenance re-evaluation with
  strictly-earlier operands (281-312) → prefix fixation → nesting as
  image arrays → orbit trees (root, distinctness, edge validity,
  reachability with cycle bound) → closure → transversal images →
  Schreier residues sifted through lower levels → input membership.
  Product/inverse/transporter orders were re-derived against the spec §3
  convention (`(pq)[v]=q[p[v]]`): `cc_compose`, `transporter`,
  `times_transporter_inverse` and the sift step are mutually consistent
  and correct. No way was found for a verified chain to certify a group
  other than `⟨inputs⟩`, and root inputs are byte-compared with the
  header generator list (`cc_check.c:567-571`).
- **G-stage re-derivation** (`cc_check.c:150-244`): `F` recomputed from
  the checker's own partition; `u` must be a bijection and a member of the
  root group; `M = F^u` recomputed; the rebased chain must verify,
  generate the *same* group by mutual membership of generating sets, have
  base prefix `M`, and have `M[i]` the least point of level `i`'s orbit —
  which, by the §7.2 greedy argument, does characterise `M = min F^G`.
  Orbit ranking by least point via union-find (`cc_chain.c:534-563`)
  matches "sort each orbit's labels, then the lists lexicographically"
  (distinct least points decide lexicographic order of the sorted lists).
- **Refinement/branch/leaf replay**: sweep count is exact (continue iff
  a strict split, always ≥ 1 sweep, `cc_check.c:437-455`); target cell is
  the `(size, position)` minimum (311-318); children must enumerate the
  cell as a set with no repeats and exactly cell-size entries
  (339-410); pruned children need an explored representative (chains and
  self-representatives are rejected by the post-loop state check), an
  in-table automorphism fixing the prefix pointwise and mapping
  representative to child; leaves require discreteness and `t` equal to
  the final sweep's `u`, which at `F = L` is forced to be `t_L`. Leaf
  images and final `(trace, bytes, least-t)` are recomputed, never read
  from the certificate except for comparison; trailing bytes rejected.
- **Integer/size discipline in the checker reader paths**: every count
  taken from the stream is checked against remaining bytes *before* an
  array is sized from it (generators, automorphisms, chain inputs and
  generator rows, CDAG records, the `4n`-bytes-final-witness bound on
  `n`); products that could wrap `size_t` are guarded before `cc_alloc`;
  signature-count and duplicate-arc additions overflow to `CAPACITY`,
  not wraparound (`cc_refine.c:158-160`, `cc_object.c:180-182`). Recursion
  depth in `node()` is bounded by `n` in effect (each branch strictly
  increases the cell count, branches at a discrete partition are
  rejected, and `prefix[depth]` writes occur only in a branch, where
  `depth < cells ≤ n`) — reasoned invariant, not a locally asserted one;
  a future refactor could break it silently, worth an explicit
  `depth < n` guard as defence in depth.
- **API ownership/aliasing on the paths read**: builders copy (`api.c`
  passim); problems retain group/object/target and copy `rho`
  (906-934); results own copies of every byte array and retain the
  problem inputs for `verify_witness` (1002-1088); status-only results
  carry no flags/traces (1005-1013); out-pointers are set `NULL` before
  validation in every creator inspected; `canon_solve_batch`
  (1292-1317) implements spec §17 literally, including call-level
  rejection writing nothing and per-entry `NULL` problems. The sink loop
  never skips/duplicates on pause or fail (1566-1586) and treats
  `accepted > chunk` and `accepted == 0` on continue as `OUTPUT_ERROR`.
- **Checker code-level independence**: no include or link path from
  `checker/` into `src/` or `include/` in either build system (see C-07
  for the assurance-scope caveat).

## 5. IN PROGRESS (not yet evidenced; to append)

- Producer O-signature layout (`src/refine/p1.c`) vs checker pair layout,
  and producer signature label set vs checker label set.
- Producer quota accounting in `src/search/p1_tree.c` (NODE at discrete
  root; §8.1 visit unit; singleton-orbit shortcut use) vs C-09 readings.
- `src/search/certificate.c` root-chain provenance rules vs checker
  assumptions (e.g. unused provenance operand fields `b` for kinds 1/2,
  ignored by `cc_chain.c:281-303`).
- `canon_mem_free(NULL)`/ledger atomics in `src/arena/mem.*` (context
  release of NULL; concurrent `canon_memory_get`).
- `docs/implementation-plan.md` cross-checks (M4 checker gate, §17 vectors)
  beyond HANDOFF's notes.
