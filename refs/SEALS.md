# Seals

Each completed reference implementation records the commit hash that contains it. Rows are append-only: a change after sealing needs a new row, never an edit. See `README.md` for the protocol.

`Toolchain` is the exact compiler or runtime version used (for ref-b, the Julia version). `Blind w.r.t.` lists the paths the author had not read before sealing; a reference that is not blind with respect to some path says so (ref-a is not blind with respect to `checks/` and `tools/`).

| Implementation | Language | Toolchain | Author | Blind w.r.t. | Sealed commit | Date |
|---|---|---|---|---|---|---|
| ref-a: the production C path (`libcanon` + `tools/canon-cli`, work policy 0x0001, deterministic witness) through `refs/ref-a/run_cases.py` | C17 (adapter: Python 3 standard library) | gcc 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1), also clang 18.1.3; Python 3.13.16; Linux x86-64 | The implementation sessions S1–S8 and M0a (maintainer Tobias J. Osborne with Claude agents), see `git log` | Not blind w.r.t. `checks/`, `tools/`, `tests/` (its own test harness); shares no code with `refs/ref-b/` and read nothing of it | `9b1a665` (M0a landing commit; the WP0.7 switches were removed before it) | 2026-10-13 |
