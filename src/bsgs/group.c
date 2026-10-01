/* The canon_group handle shared by every group backend (spec section 17: opaque retain/release
 * handles; immutable groups can be shared).  Backends construct handles only through
 * canon_group_alloc, so the reference-count invariant lives here, not in each backend. */
#include "bsgs/group.h"

#include <stdlib.h>

canon_status canon_group_alloc(const canon_group_ops *ops, uint32_t degree, void *impl,
                               canon_group **out)
{
    *out = NULL;
    canon_group *g = malloc(sizeof *g);
    if (g == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    g->ops = ops;
    g->degree = degree;
    g->impl = impl;
    g->block = g;
    g->refs = &g->refs_storage;
    canon_ref_init(g->refs); /* one reference, owned by the creator */
    *out = g;
    return CANON_COMPLETE;
}

void canon_group_share(const canon_group *group)
{
    if (group != NULL) {
        canon_ref_retain(group->refs);
    }
}

void canon_group_unshare(const canon_group *group)
{
    if (group != NULL && canon_ref_release(group->refs)) {
        canon_group *owned = group->block; /* the allocation, now unreferenced */
        owned->ops->destroy(owned->impl);
        free(owned);
    }
}

/* Public retain/release (include/canon/canon.h). */
void canon_group_retain(canon_group *group)
{
    canon_group_share(group);
}

void canon_group_release(canon_group *group)
{
    canon_group_unshare(group);
}
