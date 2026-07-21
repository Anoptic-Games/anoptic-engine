/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The script extension: tokenize / parse / validate / compile. STUB.
//
// TODO(W9, M16): compile source into a bytecode plane-set block (code, constants, symbols).
// Registers itself as a res_ext ('SBCD'). The compiler PROVES max_stack; the VM re-checks
// it, because a bytecode block can arrive from a pack and the proof did not come with it.

#include <anoptic_res_script.h>

#include "../resources_ext.h"
#include "../resources_internal.h"

anores_t ano_resscript_compile(ano_res_lifetime lifetime, const ano_res_read *read,
                               anores_t src)
{
    (void)lifetime; (void)read; (void)src;
    return (anores_t){0};                       // TODO(W9, M16)
}

anoresscript_program ano_resscript_view(const ano_res_read *read, anores_t program)
{
    (void)read; (void)program;
    return (anoresscript_program){0};           // TODO(W9, M16)
}
