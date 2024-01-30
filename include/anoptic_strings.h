/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_STRINGS_H
#define ANOPTICENGINE_ANOPTIC_STRINGS_H

#include <stdint.h>
#include <anoptic_memory.h>

// Anoptic String API
int autoStringTest();

typedef struct {
    char* buffer;   // "adadad----42th"
    uint32_t len;   // 42
} anostr_t;

// Cleanup Utilities
// Frees the anostr_t.buffer allocation.
void anostr_cleanup(anostr_t *in);
void anostrbuf_cleanup(char **in);

// UTF-8 Support
// Get the length of a character buffer in bytes.
int anostr_bytelen();

// Get the number of characters in a UTF-8 string.
int anostr_utflen();

// Check if a UTF-8 string is valid.
// Returns True if valid, False if not.
bool anostr_utfcheck();

// Convert a wchar UTF-16 string to a standard UTF-8 string.
int anostr_utf16_to_utf8();

// Unmanaged string slices.
anostr_t anostr_byteslice(anostr_t in, uint32_t startByte, uint32_t endByte);
anostr_t anostr_utfslice(anostr_t in, uint32_t startChar, uint32_t endChar);

// Managed string slice macros with scoped de-allocation. // TODO: WIP
// Stack Slices
#define ANOSTR_STACK_BYTESLICE(in, startByte, endByte) \
    ({ \
        anostr_t _slice = anostr_byteslice(in, startByte, endByte); \
        char* _stack_mem = (char*)ano_salloc(_slice.len); \
        memcpy(_stack_mem, _slice.buffer, _slice.len); \
        (anostr_t){ .buffer = _stack_mem, .len = _slice.len }; \
    })

#define ANOSTR_STACK_UTFSLICE(in, startChar, endChar) \
    ({ \
        anostr_t _slice = anostr_utfslice(in, startChar, endChar); \
        char* _stack_mem = (char*)ano_salloc(_slice.len); \
        memcpy(_stack_mem, _slice.buffer, _slice.len); \
        (anostr_t){ .buffer = _stack_mem, .len = _slice.len }; \
    })

// Managed Heap Slices
#define ANOSTR_HEAP_BYTESLICE(in, startByte, endByte) \
    ({ \
        anostr_t _slice = anostr_byteslice(in, startByte, endByte); \
        char* _heap_mem CLEANUPATTR(anostrbuf_cleanup) = mi_malloc(localHeap, _slice.len); \
        memcpy(_heap_mem, _slice.buffer, _slice.len); \
        (anostr_t){ .buffer = _heap_mem, .len = _slice.len }; \
    })

#define ANOSTR_HEAP_UTFSLICE(in, startChar, endChar) \
    ({ \
        anostr_t _slice = anostr_utfslice(in, startChar, endChar); \
        char* _heap_mem CLEANUPATTR(anostrbuf_cleanup) = mi_malloc(_slice.len); \
        memcpy(_heap_mem, _slice.buffer, _slice.len); \
        (anostr_t){ .buffer = _heap_mem, .len = _slice.len }; \
    })

#endif //ANOPTICENGINE_ANOPTIC_STRINGS_H
