/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The stack VM. STUB.
//
// TODO(W9, M16): every instruction burns one unit of fuel, so a HOSTILE SCRIPT MUST
// TERMINATE -- exhaustion is ANO_SCRIPT_OUT_OF_FUEL, a clean refusal, never a hang. Stack
// depth is proven by the compiler AND re-checked here, because a bytecode block can arrive
// from a pack without the proof. Checked against an independent reference evaluator.

#include <anoptic_script.h>

#include <stddef.h>

struct ano_script_vm {
    const anoresscript_program *program;
    const ano_script_host      *hosts;
    size_t                      host_count;
    void                       *ctx;
    uint32_t                    fuel_used;
};

ano_script_vm *ano_script_make(const anoresscript_program *program,
                               const ano_script_host *hosts, size_t host_count, void *ctx)
{
    (void)program; (void)hosts; (void)host_count; (void)ctx;
    return NULL;                                // TODO(W9, M16)
}

void ano_script_destroy(ano_script_vm *vm)
{
    (void)vm;                                   // TODO(W9, M16)
}

ano_script_status ano_script_run(ano_script_vm *vm, uint32_t fuel, double *out)
{
    (void)vm; (void)fuel; (void)out;
    return ANO_SCRIPT_INVALID_PROGRAM;          // TODO(W9, M16)
}

uint32_t ano_script_fuel_used(const ano_script_vm *vm)
{
    return vm != NULL ? vm->fuel_used : 0u;
}

const char *ano_script_status_name(ano_script_status s)
{
    switch (s) {
    case ANO_SCRIPT_OK:              return "ok";
    case ANO_SCRIPT_OUT_OF_FUEL:     return "out-of-fuel";
    case ANO_SCRIPT_STACK_OVERFLOW:  return "stack-overflow";
    case ANO_SCRIPT_STACK_UNDERFLOW: return "stack-underflow";
    case ANO_SCRIPT_BAD_OPCODE:      return "bad-opcode";
    case ANO_SCRIPT_BAD_OPERAND:     return "bad-operand";
    case ANO_SCRIPT_DIV_BY_ZERO:     return "div-by-zero";
    case ANO_SCRIPT_HOST_REFUSED:    return "host-refused";
    case ANO_SCRIPT_INVALID_PROGRAM: return "invalid-program";
    }
    return "unknown";
}
