# include/

**Purpose.** Public C17 headers of the `canon` library: `include/canon/canon.h` (opaque-handle API) and `include/canon/canon_version.h`.

**What may live here.** Only installable public declarations: enums, constants, opaque typedefs, prototypes, each carrying a one-line comment that cites its spec section. No internal types, no inline algorithms, no third-party headers.

**Governing spec sections.** §3 and §3.2 (objective tags, status, result flags), §4.1/§4.3/§4.4/§7.1 (frozen identifiers), §17 (API and ownership).

**Status.** Everything except `canon_version()` and `canon_version_string()` is a stub until M4; argument lists of stubbed functions are provisional.
