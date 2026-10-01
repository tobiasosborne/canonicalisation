/* Independent certificate checker (docs/specification.md section 20).
 * Deliberately depends on nothing but the C standard library: it must not link the canon
 * library and must not include anything from src/ or include/ (see checker/README.md). */
#include <stdio.h>

int main(void)
{
    puts("canon-check: no certificate rules implemented (M4)");
    return 2;
}
