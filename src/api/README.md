# src/api

**Purpose.** Public entry points, handle lifecycle, status and result flags, wrappers.

**Governing spec sections** (`docs/specification.md`, normative): §17 (API, ownership, failures), §3.2 (status and flags), §3 (objectives).

**Status.** Implemented in S1: `api.c` (context and capacity descriptor, retain/release for group, object, problem, workspace and result, `canon_group_create` on the explicit backend, `canon_object_create_subset`, from S2 `canon_object_create_graph` and `canon_object_create_simple_graph` (objects are `canon_root`s), `canon_problem_create` with its capacity argument (output size: exact subset or graph stream length), `canon_workspace_create`, `canon_solve` for `CANONICAL_IMAGE`/P1/CDAG-2/CDAG-BYTE-1, the result accessors and `canon_result_encode` with a sink), `version.c`, and `stubs.c` for the entry points of later slices (`canon_object_create` S5, `canon_solve_batch` S8, `canon_result_verify_witness` S4, checkpoints M6). All signatures are provisional until **M4**. Public declarations live in `include/canon/canon.h`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
