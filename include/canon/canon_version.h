/* Version constants for the canon library.  Governing text: docs/specification.md (v2.1). */
#ifndef CANON_CANON_VERSION_H
#define CANON_CANON_VERSION_H

#define CANON_VERSION_MAJOR 0
#define CANON_VERSION_MINOR 0
#define CANON_VERSION_PATCH 1

/* Version of docs/specification.md that this tree targets. */
#define CANON_SPEC_VERSION "2.1"

/* Must agree with MAJOR.MINOR.PATCH above (checked by tests/c/test_version.c). */
#define CANON_VERSION_STRING "0.0.1"

/* Packed form returned by canon_version(): (major << 16) | (minor << 8) | patch. */
#define CANON_VERSION_PACK(major, minor, patch)                                                    \
    ((((unsigned long)(major)) << 16) | (((unsigned long)(minor)) << 8) | ((unsigned long)(patch)))

#define CANON_VERSION_NUMBER                                                                       \
    CANON_VERSION_PACK(CANON_VERSION_MAJOR, CANON_VERSION_MINOR, CANON_VERSION_PATCH)

#endif /* CANON_CANON_VERSION_H */
