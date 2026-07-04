/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Extension to the Anoptic String API for UTF-8 Support.
//
// UTF-8 for storage, always. anostr_t is byte-transparent, so compare/hash/find/split/
// intern already work on UTF-8. UTF-8 memcmp order IS code point order.
// UTF-16 exists only at OS boundaries. Convert there, never store it.
// Full code space 0..0x10FFFF, no script curation. Case and classification tables are
// generated from the UCD by tools/gen_unicode_tables.c (~74 KB, two-stage, deduped).
// Everything here is total. Malformed bytes decode as U+FFFD and advance one byte.
// Case mapping a caseless rune is the identity. No error paths anywhere.
// Simple case mapping only (1:1). No locale rules, no round-trip promise (final sigma).
// Positions are byte offsets. Rune-at-index invites quadratic loops, so it does not exist.
// Grapheme clusters (UAX #29) wait until a text-editing widget needs them.

#ifndef ANOPTICENGINE_ANOPTIC_STRINGS_UTF_H
#define ANOPTICENGINE_ANOPTIC_STRINGS_UTF_H

#include <uchar.h>

#include "anoptic_strings.h"

/* Types */

// A decoded code point (scalar value): 0..0x10FFFF minus surrogates. Decode yields nothing else.
typedef uint32_t anorune_t;

#define ANORUNE_REPLACEMENT 0xFFFDu     // what malformed input decodes to
#define ANORUNE_MAX         0x10FFFFu

/* Functions */

// next rune: decode at byte offset *i, advance *i past it.
// Malformed input -> U+FFFD, advance exactly 1. Past end -> U+FFFD, *i clamped to len.
//     for (size_t i = 0; i < anostr_len(s); ) { anorune_t r = anostr_rune_next(s, &i); }
anorune_t anostr_rune_next(anostr_t s, size_t *i);

// previous rune: decode ending just before *i, move *i back to its start.
// Same malformed contract, stepping back exactly 1. *i == 0 -> U+FFFD, stays put.
//     for (size_t i = anostr_len(s); i > 0; ) { anorune_t r = anostr_rune_prev(s, &i); }
anorune_t anostr_rune_prev(anostr_t s, size_t *i);

// rune count (actual semantic length, rather than byte length).
// One pass. Matches an anostr_rune_next loop whenever anostr_utf8_valid(s).
size_t anostr_rune_count(anostr_t s);

// Strict well-formedness: rejects overlongs, surrogates, > U+10FFFF.
// Validate once at system boundaries. Iteration stays safe on anything regardless.
bool anostr_utf8_valid(anostr_t s);

// Encode r into buf, returning bytes written (1..4). Invalid runes encode as U+FFFD.
int anorune_encode(char buf[4], anorune_t r);

// Encode r and append. Same returns as anostr_builder_append.
int anostr_builder_append_rune(anostr_builder_t *b, anorune_t r);

// utf to uppercase
// utf to lowercase
// Identity when r has no mapping (caseless, unassigned, invalid). Never fails.
anorune_t anorune_to_upper(anorune_t r);
anorune_t anorune_to_lower(anorune_t r);

// General category L*: letters of every script, cased or not.
bool anorune_is_letter(anorune_t r);

// General category Nd: decimal digits of every script, not just ASCII 0-9.
bool anorune_is_digit(anorune_t r);

// The Unicode White_Space property: space/tab/newlines, NBSP, etc.
bool anorune_is_whitespace(anorune_t r);

// General category M*: combining marks (accents that stack onto a preceding base).
// Precomposed letters are NOT marks, so rejecting marks at input boundaries kills
// Zalgo text while every normal-keyboard language keeps working.
bool anorune_is_mark(anorune_t r);

/* Encoding conversion */

// For foreign boundaries  (Win32 W APIs, middleware).
// char16_t* casts to WCHAR*/wchar_t* on Windows. UTF-32 is just anorune_t[].
// Same totality: unpaired surrogates and invalid runes convert as U+FFFD.

// UTF-16 -> value. count in code units. Empty string on NULL src or allocation failure.
anostr_t anostr_from_utf16(mi_heap_t *heap, const char16_t *src, size_t count);

// anostr_from_utf16 over a NUL-terminated string (what Win32 hands back).
anostr_t anostr_from_utf16_cstr(mi_heap_t *heap, const char16_t *src);

// value -> NUL-terminated UTF-16 from heap. Code unit count (sans NUL) in *count if non-NULL.
// NULL on allocation failure.
char16_t *anostr_to_utf16(mi_heap_t *heap, anostr_t s, size_t *count);

// UTF-32 -> value. count in runes. Empty string on NULL src or allocation failure.
anostr_t anostr_from_utf32(mi_heap_t *heap, const anorune_t *src, size_t count);

// value -> NUL-terminated rune array from heap. Rune count (sans NUL) in *count if non-NULL.
// NULL on allocation failure.
anorune_t *anostr_to_utf32(mi_heap_t *heap, anostr_t s, size_t *count);

#endif //ANOPTICENGINE_ANOPTIC_STRINGS_UTF_H
