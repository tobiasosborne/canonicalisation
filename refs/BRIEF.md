# Brief for blind reference B (ref-b, Julia)

You are writing an independent reference evaluator for the canonical-form semantics in `docs/specification.md` (v2.0). Your output will be compared, case by case, with a second implementation (ref-a) that you will never see. The point of the exercise is to find places where the specification can be read in two ways. **Implement the rules as the specification states them. Where a sentence admits two readings, write the question down; do not pick an answer silently** (see "Questions" at the end, and add your own).

The protocol you are bound by is `refs/README.md` ("The blind protocol"). The plan context is `docs/implementation-plan.md` (row M0) and `docs/detailed-implementation-plan.md` §3 (WP0.1–WP0.8 and the M0 exit checklist). This brief does not restate those documents or the specification. Where it seems to disagree with them, they win; report the disagreement.

## 1. What you may and may not read

**Read:** `docs/specification.md` (all of it; §§2.1, 3, 3.1, 3.2, 4, 7, 8, 9.4, 11.1 and 17 are the normative core for M0, and §20 is the acceptance rule); `docs/implementation-plan.md`; `docs/detailed-implementation-plan.md` §3 only; `refs/README.md`, `refs/BRIEF.md`, `refs/CASES.md`, `refs/SEALS.md`, `refs/compare/FORMAT.md`, `refs/compare/compare.py`, `refs/vectors/README.md`, `refs/ref-b/`; and in `refs/vectors/golden.json` the **input fields only** (`refs/CASES.md` Part A marks which fields those are).

**Do not read until your implementation is sealed:** `src/`, `include/`, `checks/` (in particular `checks/review_checks.py`), `tools/`, `tests/`, `bench/`, `checker/`, `lean/`, `refs/ref-a/`, any code under `refs/oracle/`, `docs/slices/`, the rest of `docs/detailed-implementation-plan.md`, `HANDOFF.md`, `reviews/`, the repository history and commit messages, and the answer fields of `golden.json` (`expected_*`, `payload_hex`, and the result-valued keys of `convention_vectors` and `other_hand_checked`). All of these record implementation choices or answers. Do not discuss your choices with ref-a's author or with the maintainer's agents. Questions go to the maintainer in writing, and the answers are shared with both authors.

**What "blind" means for the golden vectors.** Spec §7.4 itself prints the expected traces and streams, so you will see them. Blindness does not mean you never see an answer. It means **no answer may enter your code or data as a constant.** Every trace, stream, witness, sign and group payload you emit must be computed from the rules (spec §7.4 last paragraph, §20). You may use §7.4 as a test that your derivation reproduces it. If it does not, find the rule you misread. Do not patch the output to match.

## 2. What to implement

Scope is WP0.4 in `docs/detailed-implementation-plan.md` §3. In spec terms:

- **Conventions** (spec §3). Arrays store `p[v] = v^p`. Products act left to right: `(pq)[v] = q[p[v]]`. Inversion reverses the order of factors. Test array direction, product order and inverses explicitly, using the spec §7.4 convention vectors (p=[1,0,2], q=[0,2,1]) as derivations, not as constants.
- **Action** `ATOM-TRANSPORT-1` (spec §2.1).
- **Objects**: subsets, atom tuples, coloured directed multigraphs (spec §4.1 tag `09`), and nested tuple/set/multiset DAGs with literal leaves (spec §4.1–4.2). Recognised but unavailable types return `UNSUPPORTED_ACTION` (spec §2.1).
- **Encoding**: the CDAG-2 stream (spec §4.1), extensional normalisation (§4.2), comparison and keys (§4.3), the second order `SIMPLE-UPPER-1` (§4.4), and `Group(H)` and `Perm` (§9.4).
- **Canonical form**: profile P1, **unpruned** (spec §7.1 scalar rules, §7.2 trace bytes, leaf map and least-witness selection). §7.3 states that "the unpruned evaluator above is normative". Do not implement pruning or root fast paths. Your trace is always the full §7.2 trace (`compare/FORMAT.md`, `trace_hex`).
- **Deterministic witness** (spec §3, last paragraph): when a case requests it, minimise the image array among all solutions sending x to the selected c. WP0.4 suggests computing it as the minimum over `Aut_G(x)·t` after a complete stabiliser enumeration.
- **Objectives** `0x0001`–`0x0007` (spec §3 table) using the complete reference algorithms of §8.1–8.4. This includes the typed labeling contract of §3.1 (λ = ρt is not an element of G) and the signed zero and nonzero contracts of §8.4. `0x0008`/`0x0009` are out of M0 scope: return `UNSUPPORTED_ACTION`.
- **Status and capacity**: the status enum of spec §3.2, and the logical work quota `max_search_nodes` of §11.1. It counts the fixed reference traversal, so `CAPACITY_LIMIT` is a function of the input alone.
- **Group operations** may be naive: explicit element lists for small groups, or a simple Schreier–Sims of your own writing for larger ones (WP0.4). Use no existing canonicalisation or computational group theory software.

## 3. Input and output

- **Input**: case files in the schema of `refs/CASES.md`. Your runner reads a case file and processes every case record in it (in `golden.json`, the `p1_cases` array). It ignores unknown keys and never reads answer fields.
- **Output**: one line per case in the seven-field TAB-separated record of `refs/compare/FORMAT.md`: `case_id, objective, status, trace_hex, bytes_hex, witness, group_hex`. Follow its field table and rules exactly, including the empty-versus-`-` conventions, the `;sign=` suffix and the n = 0 forms. Emit a record for every case, including non-`COMPLETE` ones.

## 4. Language and constraints

- **Julia**, standard library only (WP0.4; `refs/ref-b/README.md`). Pin and record the Julia version.
- Do not download packages on the maintainer's machine (Question 5 covers JSON parsing).
- Keep test runs light: small cases first, and the larger generated tiers only when asked.

## 5. Deliverables (all under `refs/ref-b/`)

1. Source code and a `README.md` with the exact command that turns a case file into a FORMAT file, plus the toolchain version.
2. A FORMAT file for the `golden.json` P1 cases, generated by that command.
3. Your own tests: the convention checks above, and invariance of the canonical image under the group, generator reordering and redundant generators (spec §20 lists more).
4. Any seeds you used, and the list of spec questions you raised.
5. **Seal** (`refs/README.md` step 3): commit the directory, then append one row to `refs/SEALS.md` with implementation `ref-b`, language with toolchain version, author, the sealed commit hash and the date. Rows are append-only. After sealing, a change needs a new row, not an edit.

## 6. Acceptance

You are done when `python3 refs/compare/compare.py <ref-a file> <ref-b file>` reports `AGREE` on every case of the agreed corpus. That corpus is the golden cases first, then the generated tiers T1–T3 of WP0.6 as the maintainer supplies them. In addition, `--plant` must report `PLANT DETECTED`, and the M0 exit checklist (detailed plan §3) must pass. A disagreement is first reviewed as a possible spec ambiguity (`refs/README.md` step 5; WP0.8). It is not settled by deciding which author is right. Agreement is evidence about ambiguity and bugs, not a proof of correctness (spec §20).

## Questions for the maintainer (to settle before or during commissioning)

The answers must go to both authors. Questions 1–6 and 13–14 are about this protocol. Questions 7–12 are spec readings.

1. **Case schema naming.** WP0.2 names the fields `case_id`, `generators` and `objective` (a hex tag). `golden.json` and `compare/FORMAT.md` use `id`, `group_generators`, `objective` (a name) and `objective_tag` (a decimal integer). `refs/CASES.md` documents the `golden.json` names as the schema. Should WP0.2 be amended to match?
2. **Fields no file fixes yet** (`refs/CASES.md` Part B): the transporter target y, which WP0.2 omits; the `tuple` object key; DAG payload keys for tags 1 and 4–8; `character`, `order`, `labeling`, `witness_mode`, `capacity`; and the top-level container of generated case files (does each file have a `p1_cases` array, or something else?).
3. **Witness when not requested.** FORMAT's `witness` field holds "the deterministic witness of a canonical image". `compare.py` compares every field, but M0 requires witness agreement only "when requested". Is the witness compared in `witness_mode = "any"` cases, and if so, which witness is emitted?
4. **The nine flags.** WP0.4 puts "status and all nine flags" (spec §3.2) in scope, but the seven-field FORMAT has no field for flags. Are flags compared, and where?
5. **JSON in Julia.** Julia's standard library has no JSON parser. May ref-b use a pinned JSON package (which needs a download), or must it carry its own minimal parser?
6. **The quota.** What does one unit of `max_search_nodes` count in the unpruned P1 tree and in the §8.1 enumeration (spec §11.1: "counts the fixed reference traversal")? What default applies when a case has no `capacity`? Both references must agree on these for `CAPACITY_LIMIT` to agree.
7. **`Group(1)` degree.** Spec §7.4 gives `Group(1)=01 00000000` with no stated degree, whereas `Group(Sym(2))` is stated "on degree two". Does the trivial-group payload depend on n?
8. **`SIMPLE-UPPER-1` for n ≤ 1.** Spec §4.4: what is the key layout, including any padding byte, when n ≤ 1?
9. **The labeling objective's stabiliser domain.** Spec §8.2 and §3.1: is the complete stabiliser of `CANONICAL_LABELING_COSET` computed on Ω or on D_n? This determines `group_hex`.
10. **The quota on the signed singleton-orbit route** (spec §7.3, last paragraph, and §11.1): does the quota count the reference traversal even when every generator fixes x?
11. **Verifier nesting** (spec §9.1): if you verify a chain, should each level's generator set be required to contain the next level's? The spec's verifier list does not say.
12. **Signed labeling orientation** σ_ρ (spec §3.1): is it out of M0 scope, so that a signed labeling case returns `UNSUPPORTED_ACTION`?
13. **ref-a's blindness.** `docs/detailed-implementation-plan.md` §0a item 6 makes ref-a a sealed snapshot of the production C path, which is not blind with respect to `checks/`. WP0.4 describes ref-a as a separately briefed C17 author. Which one does the comparison use? Spec §20 asks for "two blind independently written" implementations.
14. **The seal record.** WP0.4 asks for the toolchain, but `refs/SEALS.md` has no toolchain column. This brief puts the toolchain in the Language column. Is that acceptable?
