/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANO_STRINGS_H
#define ANOPTICENGINE_ANO_STRINGS_H

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
} anostr_t; // TODO: figure out what's the point of this type, exactly?

// Type encapsulating an index to a UTF-8 codepoint.
typedef struct {
    int32_t index;
    uint8_t bytesize;
} anostr_utfhandle_t; // TODO: What's the point of this type?

// Cleanup Utilities
void anostr_cleanup(anostr_t *in);
void anostrbuf_cleanup(char **in);

// Custom strncpy implementation using SIMD optimization.
int anostrncpy(const char *in, char * out, size_t max);

// Get the length of a character string in bytes.
int anostr_bytelen(const char *in, size_t max);

// Get the number of characters in a UTF-8 string.
int anostr_utflen(const char *in, size_t max);

// Returns a utfhandle (index and bytesize) of the next codepoint in UTF-8 sequence.
anostr_utfhandle_t anostr_utfnexthandle(anostr_utfhandle_t currentIndex, anostr_t utfString);

// Puts the next UTF-8 codepoint into the *out parameter. Returns the index of the codepoint's start (or -1 if invalid).
int anostr_utfnextchar(int index, const char *in, char *out, size_t max);

// Check if a UTF-8 string is valid.
bool anostr_utfstrcheck(anostr_t utfString);

// Check if a UTF-8 codepoint is valid.
bool anostr_utfcodecheck(const char *in);

// Convert a wchar UTF-16 string to a standard UTF-8 string.
int anostr_utfconv_16to8(const wchar_t *in, char *out, size_t max);

// Convert a UTF-8 string to a wchar UTF-16 string.
int anostr_utfconv_8to16(const char *in, wchar_t *out, size_t max);

// Unmanaged string slices. // TODO: hmmmm
// anostr_t anostr_byteslice(anostr_t in, uint32_t startByte, uint32_t endByte);
anostr_t anostr_utfslice(anostr_t in, size_t startChar, size_t endChar);

// Puts the slice between 'start' and 'end' in *out parameter, returns size of *out in bytes.
int anostr_byteslice(const char *in, char *out, size_t start, size_t end, size_t max);


// Managed string slice macros with scoped de-allocation. // TODO: WIP
// Stack Slices
#define ANOSTR_STACK_BYTESLICE(in, startByte, endByte, max) \
    ({                                                      \
        size_t _left = startByte < 0 ? 0 : startByte;       \
        size_t _right = endByte > max ? max : endByte;      \
        size_t _sliceSize = _right - _left;                 \
        char* _stack_mem = (char*)ano_salloc(_sliceSize);   \
        (anostr_t){ .buffer = _stack_mem, .len = _sliceSize }; \
    })

// Managed Heap Slices
#define ANOSTR_HEAP_BYTESLICE(in, startByte, endByte, max)  \
    ({                                                      \
        size_t _left = startByte < 0 ? 0 : startByte;       \
        size_t _right = endByte > max ? max : endByte;      \
        size_t _sliceSize = _right - _left;                 \
        char* _heap_mem CLEANUPATTR(anostrbuf_cleanup) = mi_malloc(_sliceSize); \
        (anostr_t){ .buffer = _heap_mem, .len = _sliceSize }; \
    })


// TODO: fix
#define ANOSTR_STACK_UTFSLICE(in, startChar, endChar) \
    ({ \
        anostr_t _slice = anostr_utfslice(in, startChar, endChar); \
        char* _stack_mem = (char*)ano_salloc(_slice.len); \
        memcpy(_stack_mem, _slice.buffer, _slice.len); \
        (anostr_t){ .buffer = _stack_mem, .len = _slice.len }; \
    })
// TODO: fix
#define ANOSTR_HEAP_UTFSLICE(in, startChar, endChar) \
    ({ \
        anostr_t _slice = anostr_utfslice(in, startChar, endChar); \
        char* _heap_mem CLEANUPATTR(anostrbuf_cleanup) = mi_malloc(_slice.len); \
        memcpy(_heap_mem, _slice.buffer, _slice.len); \
        (anostr_t){ .buffer = _heap_mem, .len = _slice.len }; \
    })

#endif //ANOPTICENGINE_ANO_STRINGS_H
