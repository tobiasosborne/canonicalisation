# checker/

**Purpose.** The independent certificate checker (spec §20): a small program that decides whether a producer's certificate (coverage, group verification, branch/bound/symmetry rules, final objective claim) is sound. It is the first assurance target, together with the proved reference semantics.

**Independence is the point.** The checker must **not** link against the `canon` library and must **not** include anything from `src/` or `include/`. It shares no code, headers, types or constants with the producer; the CMake target `canon-check` and the Makefile rule enforce this by giving it no include path and no library. If the checker and the producer share a bug, the check proves nothing.

**What may live here.** C17 sources depending only on the C standard library, plus the checker's own tests. Rules are added only after the corresponding M1-M3 rule statement exists; until then it prints `canon-check: no certificate rules implemented (M4)` and exits with status 2.

**Governing spec sections.** §20 (proof and acceptance evidence), §3.2 (evidence modes `TRUSTED_ENGINE` and `CHECKED_CERTIFICATE`), §8 (coverage rules per objective), §14.2 (coverage protocol), §18 (checkpoint trust boundary).

**Milestone.** M4 (docs/implementation-plan.md).
