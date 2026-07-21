/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Script -- a stack VM with a FUEL CAP.
//
// The cap is the whole point: a hostile script MUST terminate. Every instruction burns one
// unit of fuel; exhaustion is ANO_SCRIPT_OUT_OF_FUEL, a clean refusal, never a hang and
// never a crash. Stack depth is proven by the compiler and re-checked by the VM, because a
// bytecode block can arrive from a pack and the compiler's proof did not come with it.
//
// The VM BORROWS its program's planes out of manager memory (anoptic_res_script.h) and
// copies nothing. It is checked against an independent reference evaluator in the tests.
//
// FROZEN (freeze item 12).

#ifndef ANOPTICENGINE_ANOPTIC_SCRIPT_H
#define ANOPTICENGINE_ANOPTIC_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anoptic_res_script.h"
#include "anoptic_resources.h"

#define ANO_SCRIPT_STACK_MAX 256
#define ANO_SCRIPT_FUEL_DEF  100000u

typedef enum ano_script_op {
    ANO_OP_HALT = 0,
    ANO_OP_PUSH_CONST,   // imm = constant index
    ANO_OP_PUSH_LOCAL,   // a   = local slot
    ANO_OP_POP_LOCAL,    // a   = local slot
    ANO_OP_ADD, ANO_OP_SUB, ANO_OP_MUL, ANO_OP_DIV,
    ANO_OP_NEG,
    ANO_OP_CMP_LT, ANO_OP_CMP_LE, ANO_OP_CMP_EQ,
    ANO_OP_JMP,          // imm = signed pc delta
    ANO_OP_JMP_IF_ZERO,  // imm = signed pc delta
    ANO_OP_CALL_HOST,    // b   = symbol index, a = argument count
    ANO_OP_RET,
    ANO_OP_COUNT,
} ano_script_op;

typedef enum ano_script_status {
    ANO_SCRIPT_OK = 0,
    ANO_SCRIPT_OUT_OF_FUEL,     // the hostile-script refusal. Clean, always.
    ANO_SCRIPT_STACK_OVERFLOW,
    ANO_SCRIPT_STACK_UNDERFLOW,
    ANO_SCRIPT_BAD_OPCODE,
    ANO_SCRIPT_BAD_OPERAND,     // constant / local / symbol / jump target out of range
    ANO_SCRIPT_DIV_BY_ZERO,
    ANO_SCRIPT_HOST_REFUSED,
    ANO_SCRIPT_INVALID_PROGRAM,
} ano_script_status;

// A host binding. argc is exact; the VM refuses a call whose arity disagrees BEFORE the
// host sees a single argument. Return false to refuse (ANO_SCRIPT_HOST_REFUSED).
typedef struct ano_script_host {
    const char *name;
    uint32_t    argc;
    bool      (*fn)(void *ctx, const double *argv, double *out);
} ano_script_host;

typedef struct ano_script_vm ano_script_vm;

// Bind a program to a host table. Every symbol the program names must resolve, or make()
// refuses (NULL): an unresolved host call cannot be discovered at run time.
ano_script_vm *ano_script_make(const anoresscript_program *program,
                               const ano_script_host *hosts, size_t host_count, void *ctx);
void ano_script_destroy(ano_script_vm *vm);

// Run from pc 0 with `fuel` instructions of budget (0 = ANO_SCRIPT_FUEL_DEF).
// Output: the status; *out receives the top of stack on ANO_SCRIPT_OK.
ano_script_status ano_script_run(ano_script_vm *vm, uint32_t fuel, double *out);

// Fuel actually burned by the last run. The termination proof, made observable.
uint32_t ano_script_fuel_used(const ano_script_vm *vm);

const char *ano_script_status_name(ano_script_status s);

#endif // ANOPTICENGINE_ANOPTIC_SCRIPT_H
