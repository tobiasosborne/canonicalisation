import Lake
open Lake DSL

/-!
Lake configuration for the Canon Lean 4 project.  NOT BUILT in this environment (see README.md).

Mathlib pin: the audited snapshot, commit ed72f1faaeee9bea5805932ae309af92264cd8d1
(see review_sources/formalisation/PROVENANCE.md).  The toolchain in `lean-toolchain` is the one
recorded there for that snapshot (leanprover/lean4:v4.35.0-rc3).
-/

package Canon
  -- Options are added when M1 starts (as a `where` block).

-- mathlib4 @ ed72f1faaeee9bea5805932ae309af92264cd8d1 (audited snapshot; do not move silently)
require mathlib from git
  "https://github.com/leanprover-community/mathlib4" @ "ed72f1faaeee9bea5805932ae309af92264cd8d1"

@[default_target]
lean_lib Canon
  -- TODO(M1): globs default to Canon.lean and Canon/*.lean
