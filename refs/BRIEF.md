# Brief for blind reference B (ref-b, Julia)

You are writing an independent reference evaluator for the canonical-form semantics in `docs/specification.md` (**v2.1**, 2 October 2026; it amends the reviewed v2.0 baseline). Your output will be compared, case by case, with a second implementation (ref-a) that you will never see. The point of the exercise is to find places where the specification can be read in two ways. **Implement the rules as the specification states them. Where a sentence admits two readings, write the question down; do not pick an answer silently** (the questions raised before commissioning are answered at the end of this brief; raise your own in the same way).

The protocol you are bound by is `refs/README.md` ("The blind protocol"). The plan context is `docs/implementation-plan.md` (row M0) and `docs/detailed-implementation-plan.md` §3 (WP0.1–WP0.8 and the M0 exit checklist). This brief does not restate those documents or the specification. Where it seems to disagree with them, they win; report the disagreement.

## 1. What you may and may not read

**Read:** `docs/specification.md` v2.1 (all of it; §§2.1, 3, 3.1, 3.2, 4, 7, 8, 9.1, 9.4, 11.1 and 17 are the normative core for M0, and §20 is the acceptance rule); `docs/implementation-plan.md`; `docs/detailed-implementation-plan.md` §3 only; `refs/README.md`, `refs/BRIEF.md`, `refs/CASES.md`, `refs/SEALS.md`, `refs/compare/FORMAT.md`, `refs/compare/compare.py`, `refs/vectors/README.md`, `refs/ref-b/`; and in `refs/vectors/golden.json` the **input fields only** (`refs/CASES.md` Part A marks which fields those are).

**Do not read until your implementation is sealed:** `src/`, `include/`, `checks/` (in particular `checks/review_checks.py`), `tools/`, `tests/`, `bench/`, `checker/`, `lean/`, `refs/ref-a/`, any code under `refs/oracle/`, `docs/slices/`, the rest of `docs/detailed-implementation-plan.md`, `HANDOFF.md`, `reviews/`, the repository history and commit messages, and the answer fields of `golden.json` (`expected_*`, `payload_hex`, and the result-valued keys of `convention_vectors` and `other_hand_checked`). All of these record implementation choices or answers. Do not discuss your choices with anyone working on ref-a (the sealed C snapshot, `refs/README.md`) or with the maintainer's agents. Questions go to the maintainer in writing, and the answers are shared with both authors.

**What "blind" means for the golden vectors.** Spec §7.4 itself prints the expected traces and streams, so you will see them. Blindness does not mean you never see an answer. It means **no answer may enter your code or data as a constant.** Every trace, stream, witness, sign and group payload you emit must be computed from the rules (spec §7.4 last paragraph, §20). You may use §7.4 as a test that your derivation reproduces it. If it does not, find the rule you misread. Do not patch the output to match.

## 2. What to implement

Scope is WP0.4 in `docs/detailed-implementation-plan.md` §3. In spec terms:

- **Conventions** (spec §3). Arrays store `p[v] = v^p`. Products act left to right: `(pq)[v] = q[p[v]]`. Inversion reverses the order of factors. Test array direction, product order and inverses explicitly, using the spec §7.4 convention vectors (p=[1,0,2], q=[0,2,1]) as derivations, not as constants.
- **Action** `ATOM-TRANSPORT-1` (spec §2.1).
- **Objects**: subsets, atom tuples, coloured directed multigraphs (spec §4.1 tag `09`), and nested tuple/set/multiset DAGs with literal leaves (spec §4.1–4.2). Recognised but unavailable types return `UNSUPPORTED_ACTION` (spec §2.1).
- **Encoding**: the CDAG-2 stream (spec §4.1), extensional normalisation (§4.2), comparison and keys (§4.3), the second order `SIMPLE-UPPER-1` (§4.4), and `Group(H)` and `Perm` (§9.4).
- **Canonical form**: profile P1, **unpruned** (spec §7.1 scalar rules, §7.2 trace bytes, leaf map and least-witness selection). §7.3 states that "the unpruned evaluator above is normative". Do not implement pruning. Every output field, including the trace, comes from the unpruned evaluator: the trace is always the full §7.2 trace (`compare/FORMAT.md`, `trace_hex`). The §7.3 root shortcuts are not an output path for you; they enter only the quota count, as §11.1 states (Answer 10).
- **Deterministic witness** (spec §3, last paragraph): minimise the image array among all solutions sending x to the selected c. Every M0 corpus case requests it (`witness_mode` `"deterministic"`, `refs/CASES.md`), and `compare.py` compares the `witness` field on every case (Answer 3). WP0.4 suggests computing it as the minimum over `Aut_G(x)·t` after a complete stabiliser enumeration.
- **Objectives** `0x0001`–`0x0007` (spec §3 table) using the complete reference algorithms of §8.1–8.4. This includes the typed labeling contract of §3.1 (λ = ρt is not an element of G) and the signed zero and nonzero contracts of §8.4. `0x0008`/`0x0009` are out of M0 scope: return `UNSUPPORTED_ACTION`. A signed labeling problem (a case carrying both `character` and `labeling`) also returns `UNSUPPORTED_ACTION` (spec v2.1 §3.1; Answer 12).
- **Status and capacity**: the status enum of spec §3.2, and the logical work quota `max_search_nodes` of §11.1 as v2.1 states it: one unit per P1 `NODE` token and one per §8.1 enumerator visit, one quota per solve, and 2^20 when the case has no `capacity` (Answer 6). The count is a function of input, objective and descriptor, and so is `CAPACITY_LIMIT`; the descriptor's `work_policy` names the traversal counted, and every M0 case uses `1`, the unpruned reference traversal (spec §11.1 v2.1), so you implement no other policy. The nine §3.2 flags are out of M0 scope: only the status is compared (Answer 4).
- **Group operations** may be naive: explicit element lists for small groups, or a simple Schreier–Sims of your own writing for larger ones (WP0.4). Use no existing canonicalisation or computational group theory software.

## 3. Input and output

- **Input**: case files in the schema of `refs/CASES.md`. Your runner reads a case file and processes every case record in it: the top-level `cases` array of a generated file, or the `p1_cases` array of `golden.json` (accept both). It ignores unknown keys and never reads answer fields.
- **Output**: one line per case in the seven-field TAB-separated record of `refs/compare/FORMAT.md`: `case_id, objective, status, trace_hex, bytes_hex, witness, group_hex`. Follow its field table and rules exactly, including the empty-versus-`-` conventions, the `;sign=` suffix and the n = 0 forms. Emit a record for every case, including non-`COMPLETE` ones.

## 4. Language and constraints

- **Julia**, standard library only (WP0.4; `refs/ref-b/README.md`). Pin the Julia version and record it in the seal's `Toolchain` column.
- Do not download packages on the maintainer's machine. ref-b carries its own minimal JSON parser (Answer 5); it reads integers exactly, never through floating point (spec §4.1).
- Keep test runs light: small cases first, and the larger generated tiers only when asked.

## 5. Deliverables (all under `refs/ref-b/`)

1. Source code and a `README.md` with the exact command that turns a case file into a FORMAT file, plus the Julia version.
2. A FORMAT file for the `golden.json` P1 cases, generated by that command.
3. Your own tests: the convention checks above, and invariance of the canonical image under the group, generator reordering and redundant generators (spec §20 lists more).
4. Any seeds you used, and the list of spec questions you raised after these answers.
5. **Seal** (`refs/README.md` step 3): commit the directory, then append one row to `refs/SEALS.md` with implementation `ref-b`, language `Julia`, toolchain (the exact Julia version), author, `Blind w.r.t.` (the paths in §1's "Do not read" list that you had not read before sealing), the sealed commit hash and the date. Rows are append-only. After sealing, a change needs a new row, not an edit.

## 6. Acceptance

You are done when `python3 refs/compare/compare.py <ref-a file> <ref-b file>` reports `AGREE` on every case of the agreed corpus. That corpus is the golden cases first, then the generated tiers T1–T3 of WP0.6 as the maintainer supplies them. In addition, `--plant` must report `PLANT DETECTED`, and the M0 exit checklist (detailed plan §3) must pass. A disagreement is first reviewed as a possible spec ambiguity (`refs/README.md` step 5; WP0.8). It is not settled by deciding which author is right. Agreement is evidence about ambiguity and bugs, not a proof of correctness (spec §20).

## Maintainer answers (2 October 2026)

These answers go to both authors. Questions 1–6 and 13–14 were about this protocol, and the answers below settle them. Questions 7–12 were spec readings: each is settled by the text of **spec v2.1**, and the answer names the section that now states the rule. Read that section; where this list and the spec differ, the spec wins.

1. **Case schema naming** (WP0.2's `case_id`, `generators` and hex `objective` against `golden.json`'s `id`, `group_generators`, `objective` name and decimal `objective_tag`). **Answer:** WP0.2 is amended to the `golden.json` names. `case_id` survives only as the first FORMAT field, which equals the case's `id`.
2. **Fields no file fixed** (transporter target, tuple key, DAG payload keys, `character`, `order`, `labeling`, `witness_mode`, `capacity`, and the container of generated case files). **Answer:** `refs/CASES.md` Part B is now definitive and defines each of them. A generated case file is a JSON object whose case records are the array `cases`; `golden.json` keeps `p1_cases`; runners accept both.
3. **Witness when not requested.** **Answer:** The case does not arise in M0. Every M0 corpus case requests the deterministic witness (`witness_mode` `"deterministic"`), and `compare.py` compares the `witness` field on every case.
4. **The nine flags.** **Answer:** Out of M0 scope. Only the status is compared; the FORMAT record has no flag field and gains none.
5. **JSON in Julia.** **Answer:** ref-b carries its own minimal JSON parser; no package is downloaded. The maintainer may later supply a line format as an alternative input; until then JSON is the input.
6. **The quota.** **Answer:** One unit of `max_search_nodes` per P1 `NODE` token and one per §8.1 enumerator visit; one quota per solve; default 2^20 when a case has no `capacity`. Spec v2.1 §11.1 states the rule.
7. **`Group(1)` degree.** **Answer:** Spec v2.1 §9.4 now states the `Group(1)` payload for every degree n, and where a decoder takes n from.
8. **`SIMPLE-UPPER-1` for n ≤ 1.** **Answer:** Spec v2.1 §4.4 now states the key layout for n ≤ 1, including whether a padding byte exists.
9. **The labeling objective's stabiliser domain.** **Answer:** Spec v2.1 §8.2 and §3.1 now state on which domain the complete stabiliser of `CANONICAL_LABELING_COSET` is computed and reported.
10. **The quota on the signed singleton-orbit route.** **Answer:** Spec v2.1 §11.1 now states which traversal the quota counts, including how the §7.3 root shortcuts are counted.
11. **Verifier nesting.** **Answer:** Spec v2.1 §9.1 now lists the nesting condition between consecutive levels' generator sets among the verifier's checks.
12. **Signed labeling orientation σ_ρ.** **Answer:** Spec v2.1 §3.1 and §12 now state that σ_ρ is deferred and what an implementation returns until it supports it. M0 references return `UNSUPPORTED_ACTION` for a signed labeling problem.
13. **ref-a's blindness.** **Answer:** ref-a is the sealed snapshot of the unpruned production C path (detailed plan §0a item 6). Its seal row records that it is not blind with respect to `checks/` and `tools/`. ref-b is the blind implementation. Spec §20's "two blind independently written" implementations is read as "two independently written" implementations (`refs/README.md`).
14. **The seal record.** **Answer:** `refs/SEALS.md` now has a `Toolchain` column and a `Blind w.r.t.` column. Rows remain append-only.

Spec v2.1 also settles three readings that no question raised: §7.4 states that `(0,2)^(pq)=(2,1)` is cycle conjugation; §3.2 assigns the statuses the numeric values 0–7 in listed order; and §9.4 rule 1 states that orbit blocks are pairwise disjoint and ordered by least point, so a decoder rejects overlapping or unordered blocks as `INVALID_INPUT`.
