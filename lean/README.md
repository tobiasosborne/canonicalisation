# lean/

**Purpose.** Skeleton of the Lean 4 project (`Canon`) for plan milestone M1: kernel-checked semantic proofs of the reference semantics (convention bridge, tree canonicality, coset coverage, signed zero/nonzero, encoding injectivity).

**NOT BUILT in this environment.** Lean and Lake are not installed here, **mathlib has not been fetched**, and no build is to be attempted: see the machine-load constraints in `HANDOFF.md` (no dependency downloads, no mathlib unpacking, no heavy builds). These files are never compiled here.

**Contents.** `lakefile.lean` (package `Canon`, mathlib pinned to the audited snapshot `ed72f1faaeee9bea5805932ae309af92264cd8d1`, see `review_sources/formalisation/PROVENANCE.md`), `lean-toolchain` (`leanprover/lean4:v4.35.0-rc3`, the toolchain recorded in PROVENANCE.md for that snapshot), `Canon.lean`, and `Canon/{Convention,Action,Tree,Coset,Signed,Encoding}.lean`. Each module holds only a docstring naming the theorem(s) it will contain and a `-- TODO(M1)` marker. There are no `sorry`-filled theorem statements: docstrings only.

**Governing spec sections.** §3 and §3.1 (conventions, typed cosets), §4.1-§4.2 (encoding), §7 (P1), §8 and §8.4 (reference algorithms, signed), §20 (proof layers). Obligations: `docs/implementation-plan.md` M1.

**Rules.** No claim of a proved result before the theorem is kernel-checked in a later environment with a verified toolchain. `.lake/` is git-ignored.
