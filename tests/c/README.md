# tests/c

**Purpose.** C17 unit tests, built with the same strict warning flags as the library and run by `ctest` / `make test`.

**What may live here.** One `test_*.c` per topic, no external frameworks, exit status 0 on success. Tests may include internal headers from `src/` (the checker may not).

**Current tests.** `test_version.c` (version constants; the stubs return `CANON_UNSUPPORTED_ACTION` even for NULL arguments, a documented choice that M4 replaces with validation), `test_header_abi.c` (static asserts: tag values, 8-member status enum, flag struct).

**Governing spec sections.** §3 and §3.2 (tags, statuses, flags), §17 (API), §20.
