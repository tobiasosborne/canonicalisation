# refs/oracle

**Purpose.** A tiny exhaustive oracle (spec §20): on small domains it enumerates the whole group and checks all orbit values, lexicographic minima, complete stabilisers, transporters, characters and reference tree keys by brute force. It is independent of both blind implementations and of the production code.

**What may live here.** Deliberately naive code (full group enumeration by closure, no pruning, no BSGS) in any language, with fixed small-degree test generators. It cannot be used beyond oracle range, which is why ref-a and ref-b must also agree with each other on generated larger cases.

**Output.** Same record format as the references (`refs/compare/FORMAT.md`) so that `compare.py` can include it.

**Governing spec sections.** §20 (separate tiny exhaustive oracle), §3 (objectives and conventions), §7 (P1), §8 (reference algorithms), §8.4 (characters, signed objective).
