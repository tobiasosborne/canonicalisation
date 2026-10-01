# bench/

**Purpose.** Placeholder for the benchmark programme. No benchmark exists or is authorised yet; no calibration runs on the shared machine (HANDOFF constraints).

**Governing spec sections.** §19 (benchmark programme: families, competitors, metrics, scaling) and §12.4 (reproducible gaps). Every timing is reported as a gap to a stated lower bound L of category U (problem/contract), A (specified-algorithm work) or H (hardware model), never E (calibrated engineering estimate): Δ = T − L, ρ = T/L, δ = (T − L)/L. See `docs/performance-lower-bounds.md`.

**What may live here.** Harness sources, instance generators with seeds, raw timing records conforming to `ledger-schema.json`, and pinned-hardware descriptions. Third-party competitor sources are not redistributed (`review_sources/` policy).

**Milestone.** M5 (serial and small-batch performance), later M6/M8.
