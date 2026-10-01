# src/

**Purpose.** Library implementation, one subdirectory per module of spec §5: `api`, `object`, `encoding`, `perm`, `bsgs`, `coset`, `partition`, `refine`, `search`, `symmetry`, `scheduler`, `arena`, `checkpoint`, `cpu_dispatch`, `metrics`. The independent checker is *not* a module here: it lives in `checker/` and must never include anything from `src/`.

**Governing spec sections.** §5 (components and semantic independence) and the per-module sections named in each subdirectory README.

**Milestones.** perm/bsgs/coset: M2. object/encoding: M3. partition/refine/search/symmetry/api: M4. arena/metrics: M5. scheduler/checkpoint: M6. cpu_dispatch: M8.

**Current state.** Slice S1 (`docs/slices/S1.md`) implements the subset canonical-image path in `perm`, `bsgs` (explicit backend), `object` (subset), `encoding` (wire primitives, subset stream), `partition`, `refine`, `search`, `api`, `arena/checked.h`, `arena/refcount.h` and `util/sort` (shared stable sort); each module README says what exists. Slice S2 (`docs/slices/S2.md`) adds coloured directed multigraphs: `object` (graph import and action, root-object dispatch), `encoding` (`Nat`, graph stream, `SIMPLE-UPPER-1` key), `refine` (the O stage with sparse signatures) and `search`/`api` generalised to a root object. Slice S3 (`docs/slices/S3.md`) replaces the default group backend in `bsgs` by a verified stabiliser chain (constructor, independent verifier, provenance, sift, rebase, tuple minimum, the reference Schreier recursion for tests), adds `canon_perm_table` to `perm`, and the backend option and `canon_group_order` to `api`. Other modules are placeholders.

**What may live here.** C17 only, no dependencies. Public declarations belong in `include/canon/`; headers here are internal.
