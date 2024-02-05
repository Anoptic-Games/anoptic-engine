/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_STRINGS_H
#define ANOPTICENGINE_ANOPTIC_STRINGS_H

#include <stdint.h>
#include <string.h>
#include "anoptic_memory.h"

// Anoptic String API
int autoStringTest();

// String type encapsulating a length a char buffer.
// Valid storage for ASCII and UTF-8 strings.
typedef struct {
    char* buffer;
    size_t len;
} anostr_t;

// Immutable string type.
typedef struct {
    const char* buffer;
    size_t len;
} anostrview_t;

// Type encapsulating an index to a UTF-8 codepoint.
typedef struct {
    int32_t index;
    uint8_t bytesize;
} anostr_utfhandle_t;

// Cleanup Utilities
void anostr_cleanup(anostr_t *in);
void anostrbuf_cleanup(char **in);

// Get the length of a character string in bytes.
int anostr_bytelen(const char *in, size_t max);

// Get the number of characters in a UTF-8 string.
int anostr_utflen(const char *in, size_t max);

// Returns a utfhandle (index and bytesize) of the next codepoint in UTF-8 sequence.
anostr_utfhandle_t anostr_utfnexthdl(anostr_utfhandle_t currentIndex, anostr_t utfString);

// Puts the next UTF-8 codepoint into the *out parameter. Returns the index of the codepoint's start (or -1 if invalid).
int anostr_utfnextchar(int index, const char *in, char *out); // TODO: size_t max ??

// Check if a UTF-8 string is valid.
bool anostr_utfstrcheck(anostr_t utfString);

// Check if a UTF-8 codepoint is valid.
bool anostr_utfcodecheck(int index, const char *in);

// Convert a wchar UTF-16 string to a standard UTF-8 string.
int anostr_utfconv_16to8(const wchar_t *in, char *out);

// Convert a UTF-8 string to a wchar UTF-16 string.
int anostr_utfconv_8to16(const char *in, wchar_t *out);

// Unmanaged string slices. // TODO: hmmmm
anostrview_t anostr_byteslice(anostr_t in, uint32_t startByte, uint32_t endByte);
anostrview_t anostr_utfslice(anostr_t in, uint32_t startChar, uint32_t endChar);

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
