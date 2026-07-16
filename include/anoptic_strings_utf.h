/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UTF-8 extension to the Anoptic String API.

// UTF-8 for storage always. Byte ops already work; memcmp order IS code point order.
// UTF-16 only at OS boundaries -- convert there, never store. Malformed -> U+FFFD, advance one. Total, no error paths.
// Simple 1:1 case mapping (caseless = identity; no locale; no round-trip / final sigma).
// Positions are byte offsets. No rune-at-index.
// Collation/case/class cover: Latin European, Greek, Cyrillic, Runic, kana, Han (code point), punct.
// Everything else falls back to code point (= byte) order.

#ifndef ANOPTICENGINE_ANOPTIC_STRINGS_UTF_H
#define ANOPTICENGINE_ANOPTIC_STRINGS_UTF_H

// Shim char16_t where <uchar.h> is missing.
#if __has_include(<uchar.h>)
    #include <uchar.h>
#else
    #include <stdint.h>
    typedef uint_least16_t char16_t;
#endif

#include "anoptic_strings.h"


/* Types */

// Decoded code point: 0..0x10FFFF minus surrogates.
typedef uint32_t anorune_t;

#define ANORUNE_REPLACEMENT 0xFFFDu     // malformed input decodes to this
#define ANORUNE_MAX         0x10FFFFu


/* Functions */

// Decode at *i, advance *i past it. Malformed -> U+FFFD, advance 1. Past end -> U+FFFD, *i = len.
//     for (size_t i = 0; i < anostr_len(s); ) { anorune_t r = anostr_rune_next(s, &i); }
anorune_t anostr_rune_next(anostr_t s, size_t *i);

// Decode ending just before *i, move *i to its start. Malformed -> U+FFFD, step back 1. *i == 0 -> U+FFFD, stays.
//     for (size_t i = anostr_len(s); i > 0; ) { anorune_t r = anostr_rune_prev(s, &i); }
anorune_t anostr_rune_prev(anostr_t s, size_t *i);

// Rune count (one pass). Matches anostr_rune_next loop when anostr_utf8_valid(s).
size_t anostr_rune_count(anostr_t s);

// Strict well-formedness: rejects overlongs, surrogates, > U+10FFFF.
// Validate at system boundaries. Iteration stays safe on anything regardless.
bool anostr_utf8_valid(anostr_t s);

// Encode r into buf, returning bytes written (1..4). Invalid -> U+FFFD.
int anorune_encode(char buf[4], anorune_t r);

// Encode r and append. Same returns as anostr_builder_append.
int anostr_builder_append_rune(anostr_builder_t *b, anorune_t r);

// Case/class tables: Latin, Greek, Cyrillic, Runic, kana, Han, punctuation. Outside: caseless, no flags.

// r uppercased, or r if unmapped. Never fails.
anorune_t anorune_to_upper(anorune_t r);
// r lowercased, same identity contract.
anorune_t anorune_to_lower(anorune_t r);
// General category L* within shipped scripts.
bool anorune_is_letter(anorune_t r);

// General category Nd: ASCII 0-9 and fullwidth digits (unlisted scripts' digits are not).
bool anorune_is_digit(anorune_t r);

// Unicode White_Space: space/tab/newlines, NBSP, ideographic space.
bool anorune_is_whitespace(anorune_t r);

// General category M* (combining marks). Precomposed letters are NOT marks.
bool anorune_is_mark(anorune_t r);

// General category P*. Symbols (S*) are not punctuation (cull "+5 Sword!" keeps +).
bool anorune_is_punct(anorune_t r);


/* Collation */

// UCA/DUCET default, three levels (base, accent, case), byte order breaks ties. No locale/contractions.
// Precomposed decompose internally. Kana share base (hira/kata differ at level three).
// Script primary order: punct < digits < Latin < Greek < Cyrillic < Runic < kana,
// then unlisted (Han included) by code point.
int anostr_collate(anostr_t a, anostr_t b);

// First four nonzero primary weights of s, big-endian u64. Short strings zero-pad. Unequal keys agree with collate's sign.
uint64_t anostr_collate_prefix(anostr_t s);

// Full sort key: anostr_compare(key(a), key(b)) == anostr_collate(a, b). Empty on alloc fail.
// Layout: nonzero u16 weights big-endian per level, 0x0000 terminator, three levels, then bytes.
anostr_t anostr_collate_key(mi_heap_t *heap, anostr_t s);

// Sort values in collation order, in place. Stable (equal strings keep their order).
void anostr_sort(anostr_t *items, size_t count);

// Sort as permutation into order[0..count). items[order[i]] is nondecreasing.
void anostr_sort_idx(const anostr_t *items, size_t count, uint32_t *order);

// Sort interned symbols by collation order, in place. Stable.
// Mutates the per-symbol prefix-key cache (same one-owner-thread rule as anostr_intern).
// Out-of-range symbols sort as the empty string, matching anostr_sym_str.
void anostr_sym_sort(anostr_intern_t *t, anostr_sym *syms, size_t count);

// Base-letter equality: case- and accent-insensitive.
bool anostr_eq_base(anostr_t a, anostr_t b);

// Does s start with prefix at base-letter strength?
bool anostr_starts_base(anostr_t s, anostr_t prefix);

// Byte index of first base-letter match of needle at/after `from`. ANOSTR_NPOS if absent. Empty needle -> min(from, len).
size_t anostr_find_base(anostr_t s, anostr_t needle, size_t from);


/* Rune-class filters and per-string transforms */

#define ANOSTR_CULL_WHITESPACE 1u   // White_Space
#define ANOSTR_CULL_PUNCT      2u   // P*
#define ANOSTR_CULL_MARK       4u   // M*

// s minus every rune whose class is in `classes`. No match -> s unchanged. Empty on alloc fail.
anostr_t anostr_cull(mi_heap_t *heap, anostr_t s, uint32_t classes);

// Runes of s sorted ascending by code point. Empty on alloc fail.
anostr_t anostr_rune_sort(mi_heap_t *heap, anostr_t s);


/* Encoding conversion */

// For foreign boundaries (Win32 W APIs, middleware). Unpaired/invalid -> U+FFFD.

// UTF-16 -> value. count in code units. Empty on NULL src or alloc fail.
anostr_t anostr_from_utf16(mi_heap_t *heap, const char16_t *src, size_t count);

// anostr_from_utf16 over a NUL-terminated string.
anostr_t anostr_from_utf16_cstr(mi_heap_t *heap, const char16_t *src);

// value -> NUL-terminated UTF-16 from heap. Code unit count (sans NUL) in *count if non-NULL. NULL on fail.
char16_t *anostr_to_utf16(mi_heap_t *heap, anostr_t s, size_t *count);

// UTF-32 -> value. count in runes. Empty on NULL src or alloc fail.
anostr_t anostr_from_utf32(mi_heap_t *heap, const anorune_t *src, size_t count);

// value -> NUL-terminated rune array from heap. Rune count (sans NUL) in *count if non-NULL. NULL on fail.
anorune_t *anostr_to_utf32(mi_heap_t *heap, anostr_t s, size_t *count);

#endif //ANOPTICENGINE_ANOPTIC_STRINGS_UTF_H
