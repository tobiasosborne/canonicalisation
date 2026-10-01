/* Implements canon_version() and canon_version_string().  Build metadata only; the
 * normative contract is docs/specification.md (see CANON_SPEC_VERSION). */
#include "canon/canon.h"

uint32_t canon_version(void)
{
    return (uint32_t)CANON_VERSION_NUMBER;
}

const char *canon_version_string(void)
{
    return CANON_VERSION_STRING;
}
