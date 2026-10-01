# include/

**Purpose.** Public C17 headers of the `canon` library: `include/canon/canon.h` (opaque-handle API) and `include/canon/canon_version.h`.

**What may live here.** Only installable public declarations: enums, constants, opaque typedefs, prototypes, each carrying a one-line comment that cites its spec section. No internal types, no inline algorithms, no third-party headers.

**Governing spec sections.** §3 and §3.2 (objective tags, status, result flags), §4.1/§4.3/§4.4/§7.1 (frozen identifiers), §17 (API and ownership).

**Status.** Slice S1 implements the version functions, the context and `canon_capacity` descriptor, retain/release for every handle, `canon_group_create` (explicit backend), `canon_object_create_subset`, `canon_problem_create` (with its capacity argument), `canon_workspace_create`, `canon_solve` for `CANONICAL_IMAGE` under P1, the result accessors and `canon_result_encode`. `canon_object_create` (S5), `canon_solve_batch` (S8), `canon_result_verify_witness` (S4) and the checkpoint functions (M6) remain stubs returning `CANON_UNSUPPORTED_ACTION`. All argument lists are provisional until M4.
