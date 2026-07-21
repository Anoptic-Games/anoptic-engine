/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager -- the script extension. Tokenize / parse / validate / compile
// lives INSIDE src/resources/script/ and appears nowhere else. A compiled script is a
// plane-set block: code, constants, and the host-symbol table are three planes, so the VM
// walks them without a pointer chase and the block bakes and packs like every other.
//
// FROZEN (freeze item 12).

#ifndef ANOPTICENGINE_ANOPTIC_RES_SCRIPT_H
#define ANOPTICENGINE_ANOPTIC_RES_SCRIPT_H

#include <stdint.h>

#include "anoptic_resources.h"

#define ANORESSCRIPT_TAG_BYTECODE 0x44434253u   // 'SBCD'
#define ANORESSCRIPT_VERSION      1u

// One instruction. Fixed-width by design: a hostile file cannot desynchronize the decoder
// by claiming a variable length, so the fetch loop has no length-validation branch at all.
typedef struct anoresscript_op {
    uint8_t  op;        // ano_script_op
    uint8_t  a;
    uint16_t b;
    int32_t  imm;
} anoresscript_op;

// A compiled script view: three planes borrowing manager memory, valid until the handle's
// generation retires. A zeroed struct means stale, sentinel, or failed validation.
typedef struct anoresscript_program {
    const anoresscript_op *code;      uint32_t code_count;
    const double          *constants; uint32_t constant_count;
    const char            *symbols;   uint32_t symbol_bytes;  // NUL-separated host-binding names
    uint32_t               symbol_count;
    uint32_t               max_stack;                         // proven by the compiler, checked by the VM
} anoresscript_program;

// Compile a source resource into a bytecode block. src is a live handle to the source text
// (ano_res_get). The program becomes an owned resource under
// res_rid_derived(src_rid, 'SBCD') -- single-copy, no string key. Sentinel on a compile
// error, one log line naming line and column.
anores_t ano_resscript_compile(ano_res_lifetime lifetime, const ano_res_read *read,
                               anores_t src);

// The program view for a compiled handle. Its pointers die with read; zeroed on refusal.
anoresscript_program ano_resscript_view(const ano_res_read *read, anores_t program);

#endif // ANOPTICENGINE_ANOPTIC_RES_SCRIPT_H
