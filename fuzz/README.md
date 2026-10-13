# fuzz/

**Purpose.** Fuzz targets of slice S8 (detailed plan WP4.8; `docs/slices/S8.md` §1.7, decision D18).

| File | Target |
|---|---|
| `fuzz_cdag_decode.c` | the CDAG-2 decoder, normaliser and validator through the public API (`canon_object_create`, `canon_stream_validate`), then a bounded P1 solve of a decoded object under the trivial group; the first four bytes select the degree modulo 9 |
| `fuzz_checker.c` | `canon-check`'s rules (`cc_check`) on arbitrary bytes; compiled with the checker's own sources and nothing from `src/` |
| `fuzz_driver.h` | the shared driver: each target defines `LLVMFuzzerTestOneInput`; built with `-fsanitize=fuzzer` the libFuzzer runtime drives it; otherwise `CANON_FUZZ_MAIN` supplies a `main` that replays every file given on the command line as is and under deterministic xorshift mutations (bit flips, byte sets, truncations, insertions, span duplications), so that the harness runs with no runtime downloaded (HANDOFF: no dependency downloads) |
| `run.sh` | `sh fuzz/run.sh BUILD_DIR ITERATIONS`: extracts the six spec §7.4 streams of `refs/vectors/golden.json` as seeds, produces two certificates with `canon-cli`, and replays both targets; exit 0 iff no target crashed |

Any status or verdict is acceptable; a crash, a sanitizer report or a leak is a finding. `make test` runs the replay with `FUZZ_ITER` (default 2000) mutations per seed; `make fuzz` runs it alone; `ctest` runs `fuzz_replay` with 500. The sanitizer build (`make SANITIZE=1`, `cmake -DCANON_SANITIZE=ON`) is the one that catches memory errors. With a clang that ships libFuzzer, `clang -fsanitize=fuzzer,address -Iinclude fuzz/fuzz_cdag_decode.c build/make/libcanon.a` builds a coverage-guided fuzzer from the same file (not done here: this container's clang has no fuzzer runtime).

**What may live here.** C17 harnesses depending on the public header or the checker's sources only, the replay driver and this README; no corpus files are committed (the seeds are derived from `golden.json` and the CLI at run time).
