/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "ano_strings.h"

#include <stdio.h>
#include <stdlib.h>
#include "anoptic_memory.h"

// Anoptic Strings Implementation
void anostr_cleanup(anostr_t *in) {
    // TODO: Add additional logic to check if it's actually managed by mimalloc.
    mi_free(in->buffer);
}
// Custom strncpy implementation using SIMD optimization.
int anostrncpy(const char *in, char * out, size_t max) {

    return 0;
}

// Get the length of a character string in bytes.
int anostr_bytelen(const char *in, size_t max) {

    return 0;
}

// Get the number of characters in a UTF-8 string.
int anostr_utflen(const char *in, size_t max) {

    return 0;
}

// Returns a utfhandle (index and bytesize) of the next codepoint in UTF-8 sequence.
anostr_utfhandle_t anostr_utfnexthandle(anostr_utfhandle_t currentIndex, anostr_t utfString) {

    return (anostr_utfhandle_t){ 0, 0};
}

// Puts the next UTF-8 codepoint into the *out parameter. Returns the index of the codepoint's start (or -1 if invalid).
int anostr_utfnextchar(int index, const char *in, char *out, size_t max) {

    return 0;
}

// Check if a UTF-8 string is valid.
bool anostr_utfstrcheck(anostr_t utfString) {

    return false;
}

// Check if a UTF-8 codepoint is valid.
bool anostr_utfcodecheck(const char *in) {

    return false;
}

// Convert a wchar UTF-16 string to a standard UTF-8 string.
int anostr_utfconv_16to8(const wchar_t *in, char *out, size_t max) {

    return 0;
}

// Convert a UTF-8 string to a wchar UTF-16 string.
int anostr_utfconv_8to16(const char *in, wchar_t *out, size_t max) {

    return 0;
}

// Unmanaged string slices. // TODO: hmmmm
// anostr_t anostr_byteslice(anostr_t in, uint32_t startByte, uint32_t endByte);
anostr_t anostr_utfslice(anostr_t in, size_t startChar, size_t endChar) {


    return (anostr_t) { "hello\0", 5};
}

// Puts the slice between 'start' and 'end' in *out parameter, returns size of *out in bytes.
int anostr_byteslice(const char *in, char *out, size_t start, size_t end, size_t max) {

    return 0;
}