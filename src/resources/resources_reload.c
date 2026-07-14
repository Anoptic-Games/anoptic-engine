/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Hot reload. STUB.
//
// TODO(W5, M15): republish_locked is ONE release-store of the new pub over the old, NEVER
// NULL in between -- ano_res_release's store-NULL-then-restore would let a reader acquire a
// sentinel for a fully live resource and break old-complete-or-new-complete.
//
// Candidate (winning-source identity + rmos_stat_hint) -> confirm (res_hash_file against
// bind->content_hash; mtime is a FILTER, it lies on 9P/SMB) -> publish out of place -> full
// validate -> republish. Plus the DERIVED CASCADE: without it, reloading models/x.gltf
// leaves the stale conditioned scene published and serving old geometry forever -- hot
// reload that appears to work and is lying.

#include "resources_internal.h"

int ano_res_reload_poll(anores_t *changed, int cap)
{
    (void)changed; (void)cap;
    return 0;                                   // TODO(W5, M15): nothing changes until it lands
}
