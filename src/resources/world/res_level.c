/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Level extension. STUB. open/close refuse. view returns zeroed.

#include <anoptic_res_world.h>

#include "../resources_internal.h"

int ano_reslevel_open(const char *logical, ano_res_lifetime *out_domain, anores_t *out_level)
{
    (void)logical; (void)out_domain; (void)out_level;
    return -1;                                  // STUB
}

int ano_reslevel_close(ano_res_lifetime domain)
{
    (void)domain;
    return -1;                                  // STUB
}

anoreslevel ano_reslevel_view(const ano_res_read *read, anores_t level)
{
    (void)read; (void)level;
    return (anoreslevel){0};                    // STUB
}
