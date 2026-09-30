# Formalisation source provenance

Retrieved on 29 September 2026 for the architecture review. These local files, rather than web summaries, supply the evidence used in `reviews/formalisation-literature-review.md`.

| Artifact | Primary download URL | SHA-256 |
|---|---|---|
| `downloads/2112.14303v5-source.tar` | `https://arxiv.org/src/2112.14303v5` | `e52502c6caec30e200e525a4838ad884a196c38fbb6e320569fcb416d045ebc5` |
| `downloads/2112.14303v5.pdf` | `https://arxiv.org/pdf/2112.14303v5` | `c6a1ae32427931b54535733646d8c9f9cc4760e9b2f377d9ea5784b90ccd6853` |
| `downloads/isocert-0b160702.tar.gz` | `https://codeload.github.com/milanbankovic/isocert/tar.gz/0b160702bc0196739915541478fdfc9bb67a35db` | `4df79344a5615280aa5cba68f6d976b37a77f11768d5b4181101458d99b88df9` |
| `downloads/mathlib-ed72f1fa.tar.gz` | `https://codeload.github.com/leanprover-community/mathlib4/tar.gz/ed72f1faaeee9bea5805932ae309af92264cd8d1` | `7f72ac194afe6f047459ffd97fbb483c32aaafd2981bb585b7556a464078ca09` |

Paper: Milan Banković, Ivan Drecun, Filip Marić, *A proof system for graph (non)-isomorphism verification*, arXiv:2112.14303v5 / Logical Methods in Computer Science 19(1):9, 2023. Extracted TeX and accompanying assets are in `papers/isocert-2112.14303v5`; `paper-pdf.txt` was generated locally with `pdftotext -layout`.

Isocert repository revision: `0b160702bc0196739915541478fdfc9bb67a35db`. The public API revision response is retained in `downloads/isocert-head.json`. Its archive is extracted in `code/isocert` with the common archive root removed.

Mathlib revision: `ed72f1faaeee9bea5805932ae309af92264cd8d1`. The public API revision response is retained in `downloads/mathlib-head.json`. The pinned archive contains `lean-toolchain` naming `leanprover/lean4:v4.35.0-rc3`. The local extraction in `mathlib/mathlib4` is now sparse: eight complete Lean source files plus `LICENSE` and `lean-toolchain`. All cited source paths are preserved. The full compressed archive and its SHA-256 remain available for recovery; archive member paths are retained in `mathlib/archive-paths.txt`. This snapshot was inspected, not installed or compiled.

Retained Lean files: `Mathlib/Algebra/Group/End.lean`, `Mathlib/Algebra/Group/Opposite.lean`, `Mathlib/Algebra/Opposites.lean`, `Mathlib/Data/Finset/Max.lean`, `Mathlib/GroupTheory/Coset/Basic.lean`, `Mathlib/GroupTheory/GroupAction/Basic.lean`, `Mathlib/GroupTheory/GroupAction/Defs.lean`, and `Mathlib/GroupTheory/Schreier.lean`.

Extraction checked archive paths for absolute paths and `..` traversal, and wrote regular files only. Isocert/paper extraction rejected non-file/non-directory members; mathlib extraction selected regular Lean source/metadata members only. No downloaded source command, Makefile, Isabelle theory session, Lean code, or build tool was executed. No package or theorem-prover installation was performed.

The initial mathlib availability search used the terms `schreier.?sims`, `BSGS`, `stabilizer.chain`, `stabiliser.chain`, `strong.generating.set`, `strong.generators`, `canonical.labell`, `canonical.label`, and `graph.canon` over the full extracted `Mathlib` Lean source tree before pruning. It found only two unrelated category-theory strong-generator headings. This bounded negative search is not a claim that no relevant project exists elsewhere. Broad extraction/search stopped after the user reported local CPU contention; subsequent inspection used targeted reads only.

At the user's request, the generated mathlib extraction was pruned in one bounded cleanup command at `nice -n 19`, restricted to `mathlib/mathlib4`, using file metadata only. Before: 8,554 files, 98,640,371 file bytes, 121,577,472 allocated bytes including directories. After: 10 files, 149,729 file bytes, 208,896 allocated bytes including directories. The 8,544 unused generated files were removed; retained Lean declarations remain in their full original source files. No source-content scan or build accompanied cleanup.
