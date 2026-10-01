# src/

**Purpose.** Library implementation, one subdirectory per module of spec §5: `api`, `object`, `encoding`, `perm`, `bsgs`, `coset`, `partition`, `refine`, `search`, `symmetry`, `scheduler`, `arena`, `checkpoint`, `cpu_dispatch`, `metrics`. The independent checker is *not* a module here: it lives in `checker/` and must never include anything from `src/`.

**Governing spec sections.** §5 (components and semantic independence) and the per-module sections named in each subdirectory README.

**Milestones.** perm/bsgs/coset: M2. object/encoding: M3. partition/refine/search/symmetry/api: M4. arena/metrics: M5. scheduler/checkpoint: M6. cpu_dispatch: M8.

**Current state.** Real code exists only in `api/version.c`, `api/stubs.c` and, as declarations only, `perm/perm.h`.

**What may live here.** C17 only, no dependencies. Public declarations belong in `include/canon/`; headers here are internal.
