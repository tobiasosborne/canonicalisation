#!/bin/sh
# Slice S8 (docs/slices/S8.md 1.7, D18): replay the fuzz targets on the seed corpus with
# deterministic mutations.  Arguments: BUILD_DIR ITERATIONS.  The CDAG-2 target reads the six
# spec 7.4 streams of refs/vectors/golden.json (extracted here with python3) and the checker
# target two certificates produced by canon-cli on the spot.  Exit status 0 iff no target
# crashed.
set -eu
BUILD="${1:-build/make}"
ITER="${2:-2000}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS="$BUILD/fuzz-corpus"
mkdir -p "$CORPUS"
python3 - "$ROOT/refs/vectors/golden.json" "$CORPUS" <<'PY'
import json, struct, sys
golden = json.load(open(sys.argv[1], encoding="utf-8"))
for i, case in enumerate(golden["p1_cases"]):
    data = struct.pack(">I", case["n"]) + bytes.fromhex(case["expected_stream_hex"])
    open(f"{sys.argv[2]}/stream{i}.bin", "wb").write(data)
PY
"$BUILD/canon-cli" p1-subset --n 4 --gens "1,2,3,0;1,0,2,3" --atoms "0" --id f --prune on \
  --certificate "$CORPUS/cert0.bin" > /dev/null
"$BUILD/canon-cli" p1-graph --n 3 --gens "1,2,0;1,0,2" --arcs "0,1,,1;1,2,,1;2,0,,2" --id f \
  --prune off --certificate "$CORPUS/cert1.bin" > /dev/null
"$BUILD/fuzz_cdag_decode" "$ITER" "$CORPUS"/stream*.bin
"$BUILD/fuzz_checker" "$ITER" "$CORPUS"/cert*.bin
