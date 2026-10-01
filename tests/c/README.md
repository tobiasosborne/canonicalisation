# tests/c

**Purpose.** C17 unit tests, built with the same strict warning flags as the library and run by `ctest` / `make test`.

**What may live here.** One `test_*.c` per topic, no external frameworks, exit status 0 on success. Tests may include internal headers from `src/` (the checker may not).

**Current tests.** `test_version.c` (version constants; the remaining stubs return `CANON_UNSUPPORTED_ACTION` even for NULL arguments, while the S1 entry points return `CANON_INVALID_INPUT` for NULL arguments), `test_header_abi.c` (static asserts: tag values, 8-member status enum, flag struct), and from slice S1: `test_perm.c` (§7.4 convention vectors, inverse/product identities on random permutations), `test_wire.c` (U16/U32/B, §4.3 order, subset streams), `test_group_explicit.c` (|Sym(n)| = n! for n ≤ 5, |C₃| = 3, hand-computed `tuple_min`, capacity refusal), `test_partition.c` (split, individualisation, rollback, invariants), `test_search_subset.c` (the four §7.4 subset cases through the public API, capacity limits, invalid input, unsupported requests, `result_encode` and its sink, release in every order). `check.h` is the shared assertion helper.

**Governing spec sections.** §3 and §3.2 (tags, statuses, flags), §§4.1-4.3, §7, §10, §11.1, §17 (API), §20.
