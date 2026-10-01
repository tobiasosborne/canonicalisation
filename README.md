# canonicalisation

**An exact, native C engine for canonical forms under arbitrary finite permutation groups: specification, reviews and implementation plan.**

> **Status: design stage, scaffolded.** This repository contains a reviewed architecture specification (v2.0), a milestone plan and a detailed work-package plan, lower-bound performance analysis, literature and formalisation audits, finite mathematical sanity checks, and a build/test scaffold with the public header, machine-readable golden vectors and the blind-reference comparison harness. **No production C algorithms or Lean proofs exist yet**: the C library implements only `canon_version()` and unsupported stubs. The next milestone (M0) is two blind, independent reference implementations of the frozen semantics.

## The problem

Let Ω be a finite set of *atoms*, let G ≤ Sym(Ω) be a permutation group given by generators, and let x be a finite object on which G acts. The object might be a subset, a tuple, a coloured directed multigraph, a relational structure, a nested set/tuple/multiset structure, a permutation, a subgroup or a labeling coset. A **canonical form** is a function C_G with

- C_G(x) = x^g for some g ∈ G (the output is in the orbit of x), and
- C_G(x^h) = C_G(x) for every h ∈ G (equivalent inputs give identical output).

Two objects are G-equivalent exactly when their canonical forms are equal. If the canonical form has a specified byte encoding, those bytes can serve as a database key, a hash-consing key, or a deduplication key for large symbolic computations.

Special cases include:

- **Graph isomorphism / canonical labelling** (G = Sym(V)), the setting of nauty, Traces and bliss.
- **Restricted-group equivalence**, where only some relabellings are allowed: a grid group acting on subsets, a wreath product, symmetries of a code. Here unrestricted graph relabelling gives false equivalences.
- **Tensor-index canonicalisation** in computer algebra: slot symmetries, dummy-index relabelling and identical-factor exchange, with *signs* from antisymmetry and Grassmann parity. This is the setting of xPerm and of this project's sibling, [TensorGR.jl](https://github.com/tobiasosborne/TensorGR.jl).
- **Group-valued objects**: canonical representations of subgroups and labeling cosets on an ordered domain.

## Why a new engine

Existing tools each cover part of this space.

| Family | Strength | Limitation this project addresses |
|---|---|---|
| nauty, Traces, bliss (individualisation–refinement) | Very fast graph canonical labelling | Groups other than Sym(V) must be encoded as gadgets; canonical labels are defined only for a pinned configuration |
| Butler–Portugal / xPerm (double cosets) | Groups given by generators; signed tensor symmetries | Factorial behaviour on products of identical factors: 5.48 s against about 29 µs in a TensorGR.jl measurement of (R_abcd R^abcd)³ |
| Graph backtracking (Jefferson–Waldecker–Wilson; Vole) | Canonical images under *arbitrary* G | Research implementation coupled to GAP; no frozen byte format or independent certificate checker |
| Schweitzer–Wiebking general-object theory | Broad object classes, labeling cosets, strong asymptotic bounds | A theoretical framework; its bounds do not transfer to a practical backtracker |

The goal is a **dependency-free C17 core**. It should give exact semantics for broad finite-group actions, a frozen and versioned canonical byte format, and performance reported honestly as gaps to justified lower bounds. Its answers should be checkable by a small independently verified certificate checker.

## Design at a glance

The full contract is in [docs/specification.md](docs/specification.md).

- **Distinct objectives with distinct completeness proofs.** Each has its own reference algorithm. Pruning that is valid for one objective is never assumed valid for another. The objectives are:
  - canonical image;
  - lexicographically minimal image;
  - one transporter, or a proof that none exists;
  - complete stabiliser;
  - canonical labeling coset;
  - transporter coset;
  - signed canonical image;
  - constraint search.
- **Minimum image is deliberately separate.** Computing a prescribed lexicographic minimum is NP-hard in general. The spec gives an explicit Independent Set reduction. Canonical images avoid this by using a canonical search tree rather than a fixed global order.
- **A frozen, executable canonical profile.** Profile P1 (`BASE-ORBIT-GRAPH-1`) fixes initial colours, the refinement stages, the fixed-point rule, target-cell choice, trace bytes and leaf comparison. The wire grammar `CDAG-2` fixes tags, integer encodings and a canonical DAG numbering. Hand-checked golden vectors pin both.
- **Representation independence.** Results must not depend on the generating set, the stabiliser-chain base, DAG sharing, insertion order, auxiliary numbering or thread schedule. The spec states this as its own obligation, separate from equivariance.
- **Sharing-preserving output.** Nested objects are encoded as canonical DAGs, not expanded trees, so an object like xᵢ₊₁ = (xᵢ, xᵢ) stays linear in size. Subgroups are encoded by a deterministic symbolic form or a greedy canonical generating sequence of O(n² log n) entries, instead of a cubic transversal dump (about 2 PB at degree 100,000).
- **Typed labeling cosets.** For admissible labelings Λ = Gρ, the witness λ = ρt is a bijection Ω → {0,…,n−1}, not an element of G. The complete answer is Aut_G(x)·λ.
- **Signed objects.** For a character χ: G → {±1}, one verified automorphism a with χ(a) = −1 is a complete certificate that the object is zero. A nonzero answer needs complete stabiliser evidence; probabilistic symmetry search can never certify it.
- **Deterministic parallelism.** Parallel helpers may only contribute verified automorphisms or compute the same pinned function (rules R1–R3). A mutex-linearised coverage state machine defines when a search is complete. Capacity failures are a function of the input and budget, never of scheduling.
- **Honest performance accounting.** The spec keeps three categories of lower bound apart: problem/contract bounds, specified-algorithm work bounds and hardware-model bounds. Calibrated engineering estimates are a fourth quantity, never a lower bound. Every timing reports Δ = T − L, ρ = T/L and δ = (T − L)/L against a stated bound L. See [docs/performance-lower-bounds.md](docs/performance-lower-bounds.md).
- **Layered assurance.** Proof milestones are separate:
  - Lean proofs of the reference semantics;
  - certified group operations;
  - adapter proofs;
  - a certificate checker;
  - a concurrency proof;
  - C/runtime verification.

  The first assurance target is proved reference semantics plus a small checker for certificates emitted by the fast C producer.

### Conventions

Permutations are arrays `p[v] = v^p` acting on the right, with left-to-right products: `(pq)[v] = q[p[v]]` and `(x^p)^q = x^(pq)`. This is opposite to the usual composition in mathlib's `Equiv.Perm`; the plan includes a one-time proved bridge.

## Repository layout

```
README.md                          this file
LICENSE                            GNU AGPL v3
HANDOFF.md                         current state and next steps for contributors/agents
CLAUDE.md, CONTRIBUTING.md         agent and contributor rules (constraints, conventions)
Makefile, CMakeLists.txt           C17 build: library, checker, tests; strict warnings, sanitizer option
include/canon/                     public header (statuses, objective tags, frozen IDs, opaque handles)
src/<module>/                      one directory per spec §5 module; only api/version.c and api/stubs.c have code
checker/                           independent certificate checker (shares no code with src/)
refs/                              M0: blind-reference protocol, seals, oracle, interchange format,
                                   comparison script and refs/vectors/golden.json (spec §7.4 transcribed)
tests/c, tests/python              C tests (ctest) and unittest suites (golden vectors, stream parser)
tools/hexdump_stream.py            strict CDAG-2 stream dumper
bench/                             benchmark programme placeholder and Δ/ρ/δ ledger schema
lean/                              Lean 4 project skeleton for M1 (docstrings only; not built here)
docs/
  specification.md                 architecture and implementation specification, v2.0
  implementation-plan.md           milestones M0–M8, H0/H1, gates, effort judgments
  detailed-implementation-plan.md  work packages, internal interfaces, decided policies, tests, sequencing
  performance-lower-bounds.md      hardware model, lower-bound taxonomy, numeric envelopes
reviews/
  referee-report.md                referee report on v1.0 (historical record)
  review-response.md               maps all 18 findings and TensorGR items T1–T14 to decisions
  algorithm-literature-review.md   audit against primary TeX sources
  formalisation-literature-review.md  Isabelle/mathlib prior art and proof scope
  tensorgr-learnings.md            lessons from TensorGR.jl's canonicalisation prototypes
checks/
  review_checks.py                 finite mathematical sanity checks (not proofs)
review_sources/
  SOURCES.json                     inventory of cited third-party evidence (URL/archive, SHA-256)
  fetch_sources.py                 verify or rebuild that evidence locally
  */manifest*.json, PROVENANCE.md  original acquisition records
```

### Suggested reading order

1. [docs/specification.md](docs/specification.md): §§3–4 (contracts and encoding), §7 (profile P1 and its correctness proof), §8 (complete reference algorithms).
2. [reviews/referee-report.md](reviews/referee-report.md) and [reviews/review-response.md](reviews/review-response.md), which show what changed from v1.0 and why.
3. [docs/implementation-plan.md](docs/implementation-plan.md): what gets built, in which order, and what evidence each gate requires. [docs/detailed-implementation-plan.md](docs/detailed-implementation-plan.md) breaks each milestone into work packages with files, interfaces and tests.
4. [docs/performance-lower-bounds.md](docs/performance-lower-bounds.md) and the two literature reviews, for depth.

The reviewed v1.0 baseline is commit [`7ad98cb`](https://github.com/tobiasosborne/canonicalisation/tree/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c). Referee line references point there.

## Evidence policy and primary sources

Literature claims in these documents are grounded only in **local primary sources**, preferably TeX, with recorded provenance. Web summaries are treated as leads, not evidence. The cited sources are:

- arXiv papers and their TeX;
- a scan of Karp (1972);
- vendor hardware documents;
- the pinned isocert (Isabelle/HOL) repository;
- selected pinned mathlib files.

Their licences do not permit redistribution here, so **the repository tracks only their provenance**. To rebuild them locally:

```sh
python3 review_sources/fetch_sources.py --fetch   # download + extract, verified against SHA-256
python3 review_sources/fetch_sources.py           # offline verification only
```

Vendor web pages change over time and are reported as `CHANGED` rather than silently accepted. Locally derived text and page images are listed with the command that produced them. See [review_sources/README.md](review_sources/README.md).

## Build and checks

```sh
make && make test            # gcc, -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wvla -Werror
make check                   # finite sanity checks plus the Python unit tests
cmake -S . -B build && cmake --build build && ctest --test-dir build   # equivalent CMake definition
python3 checks/review_checks.py
```

The C build has no dependencies. `CANON_SANITIZE=ON` (CMake) or `SANITIZE=1` (make) adds AddressSanitizer and UndefinedBehaviorSanitizer. The Python tests verify that `refs/vectors/golden.json` reproduces the spec's §7.4 vectors through the Python model and that the stream dumper parses all six golden streams. The finite checks run in under a second. Over every subgroup of Sₙ for n ≤ 4 it checks:

- the full-list leaf-map identity;
- coset splitting;
- group-refiner normalisation;
- the Independent Set prefix reduction.

It also includes a small Python model of profile P1. Over every subgroup of Sₙ for n ≤ 3 (graphs: n ≤ 2, with loops and multiplicities) it checks:

- P1 transport invariance;
- labeling-coset reconstruction;
- signed zero/nonzero behaviour for every character;
- greedy canonical generators.

It reproduces the spec's golden traces and bytes, group payloads, DAG-sharing cases and convention vectors. These are **finite sanity checks, not proofs**. The Python model is not one of the blind reference implementations required by M0.

## Roadmap

| Milestone | Gate |
|---|---|
| M0 | Two blind reference implementations agree byte-for-byte on traces, bytes, signs and statuses; planted corruption is detected |
| M1 | Lean semantic proofs: action convention bridge, tree canonicality, coset coverage, termination, signed reference |
| M2 | Certified constructive group kernel and canonical group bytes |
| M3 | Concrete adapter and encoding proofs, one adapter at a time |
| M4 | Trustworthy scalar C plus an independently sound certificate checker |
| M5 | Serial and small-batch performance with honest ledgers against lower bounds |
| M6 | Concurrent coverage state machine: proof and runtime |
| M7 | C/runtime verification (a separate assurance track) |
| M8 | ISA kernels, then an optional GPU plugin |
| H0/H1 | Retrieve and audit `leanprover/hex-graph-iso` and `hex-perm-group` before any reuse |

Named open obligations include:

- **SIGN-COVER:** whether the arbitrary-G canonical search exposes every odd automorphism without separate stabiliser enumeration;
- **PROFILE-EQUIV:** every faster path must reproduce P1 exactly or carry a new profile ID;
- **ADAPTER-FAITHFUL:** each reduction into the engine needs its own faithfulness proof.

## How this was produced

The specification and reviews were written with AI assistance (Anthropic Claude, and OpenAI GPT/Codex agents) under the direction of Tobias J. Osborne. The v1.0 specification was refereed. Separate agents audited it against local primary sources for algorithms, formalisation and hardware. It was then revised to v2.0, and the revision was reviewed by re-deriving the golden vectors by hand and re-running the finite checks. Estimates are labelled as judgments, and unverified leads are labelled as such. Please report errors as issues.

## Licence

Copyright © 2026 Tobias J. Osborne.

The contents of this repository are licensed under the **GNU Affero General Public License v3.0 or later** (`AGPL-3.0-or-later`); see [LICENSE](LICENSE). The third-party evidence listed in `review_sources/SOURCES.json` is **not included** and remains under its owners' licences.
