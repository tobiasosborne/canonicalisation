/* Fuzz target: canon-check's rules on arbitrary bytes (spec 20; slice S8, docs/slices/S8.md
 * 1.7).  Compiled with the checker's own sources and nothing from src/ (checker/README.md).
 * Any verdict is acceptable; a crash, a sanitizer report or a leak is a finding. */
#include "../checker/cc.h"
#include "fuzz_driver.h"

cc_reason cc_check(const uint8_t *cert, size_t len, uint64_t *nodes_out);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint64_t nodes = 0;
    (void)cc_check(data, size, &nodes);
    return 0;
}
