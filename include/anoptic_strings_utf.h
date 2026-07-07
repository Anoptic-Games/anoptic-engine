/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Extension to the Anoptic String API for UTF-8 Support.
//
// UTF-8 for storage, always. anostr_t is byte-transparent, so compare/hash/find/split/
// intern already work on UTF-8. UTF-8 memcmp order IS code point order.
// UTF-16 exists only at OS boundaries. Convert there, never store it.
// Decode/encode/iterate the full code space 0..0x10FFFF.
// Everything here is total. Malformed bytes decode as U+FFFD and advance one byte.
// Case mapping a caseless rune is the identity. No error paths anywhere.
// Simple case mapping only (1:1). No locale rules, no round-trip promise (final sigma).
// Positions are byte offsets. No rune-at-index.
// Grapheme clusters (UAX #29) wait until a text-editing widget needs them.
//
// Anoptic supports lexicographic sorting (and case/classification) for these UTF subsets:
//   - English and every Latin-script European language (German, French, Romanian,
//     Swedish, Norwegian, Polish, Czech, Turkish, Vietnamese, ...)
//   - Greek, modern and polytonic/Ancient
//   - Cyrillic (Russian, Ukrainian, Serbian, Bulgarian, ...)
//   - Futhark (Runic)
//   - Japanese (kana sort in gojuon order, kanji by code point)
//   - Chinese (no alphabet exists, code point order ~ radical-stroke)
//   - punctuation, currency, full/halfwidth forms
// Everything else falls back to code point (= byte) order after the listed scripts.

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

// Case and classification tables cover the shipped scripts only:
// Latin, Greek, Cyrillic, Runic, kana, Han, punctuation
// Outside them every rune is caseless with no flags.

// r uppercased, or r when it has no mapping (caseless, unlisted, unassigned, invalid). Never fails.
anorune_t anorune_to_upper(anorune_t r);
// r lowercased, same identity contract.
anorune_t anorune_to_lower(anorune_t r);
// General category L* within the shipped scripts, cased or not.
bool anorune_is_letter(anorune_t r);

// General category Nd: ASCII 0-9 and fullwidth digits (unlisted scripts' digits are not).
bool anorune_is_digit(anorune_t r);

// The Unicode White_Space property: space/tab/newlines, NBSP, ideographic space.
bool anorune_is_whitespace(anorune_t r);

// General category M*: combining marks (accents that stack onto a preceding base).
// Precomposed letters are NOT marks. Covers 0300-036F, unlisted scripts' marks report false.
bool anorune_is_mark(anorune_t r);

// General category P*. Symbols (S*) are not punctuation: culling "+5 Sword!" keeps the +.
// Shipped scripts only (ASCII, General Punctuation, CJK, full/halfwidth forms).
bool anorune_is_punct(anorune_t r);

/* Collation */

// Lexicographic order for humans: UCA over the DUCET default table, no locale tailoring.
// Three levels: base letters first, then accents, then case. Byte order breaks ties.
// "Äpfel" < "Zebra", "resume" < "résumé", "apple" < "Apple", Cyrillic ё after е.
// Precomposed letters decompose internally: ș sorts with s, å with a.
// Kana sort together (hiragana/katakana differ at level three).
// Tables cover Latin, Greek, Cyrillic, Runic, kana, and punctuation.
// Everything else (Han included) falls back to code point order. No contractions, no locale rules.
// Script primary order: punctuation < digits < Latin < Greek < Cyrillic < Runic < kana,
// then unlisted scripts (Han, Hebrew, emoji) in code point order.
// Use for everything a player reads.
int anostr_collate(anostr_t a, anostr_t b);

// The first four nonzero primary weights of s, big-endian in one u64.
// Compute once, sort integers. Unequal keys agree with anostr_collate's sign.
// Short strings zero-pad (0 sorts before any weight).
uint64_t anostr_collate_prefix(anostr_t s);

// Full sort key: anostr_compare(key(a), key(b)) matches anostr_collate(a, b) exactly.
// Layout: each strength's nonzero weights as u16 big-endian, 0x0000 terminator, three levels, then bytes.
// ~6 bytes per rune plus the string. For repeated comparisons. Empty string on allocation failure.
anostr_t anostr_collate_key(mi_heap_t *heap, anostr_t s);

// Sort values in collation order, in place. Stable (equal strings keep their order).
// String bytes are read once, sequentially.
void anostr_sort(anostr_t *items, size_t count);

// The sort as a permutation: fills order[0..count) so items[order[i]] is nondecreasing.
// Sort structs by name without shuffling 16-byte values, gather through order instead.
void anostr_sort_idx(const anostr_t *items, size_t count, uint32_t *order);

// Sort interned symbols by their strings' collation order, in place. Stable.
// Prefix keys cache per symbol in the table.
// Mutates the cache (same one-owner-thread rule as anostr_intern).
// Out-of-range symbols sort as the empty string, matching anostr_sym_str.
void anostr_sym_sort(anostr_intern_t *t, anostr_sym *syms, size_t count);

// Base-letter equality: case- and accent-insensitive. eq_base("Ålesund", "alesund").
bool anostr_eq_base(anostr_t a, anostr_t b);

// Does s start with prefix, base letters only? Search-as-you-type against any list.
bool anostr_starts_base(anostr_t s, anostr_t prefix);

// Byte index of the first base-letter match of needle at or after `from`.
// ANOSTR_NPOS if absent. An empty needle matches at min(from, len).
size_t anostr_find_base(anostr_t s, anostr_t needle, size_t from);

/* Rune-class filters and per-string transforms */

#define ANOSTR_CULL_WHITESPACE 1u   // White_Space (space, tabs, NBSP, U+3000)
#define ANOSTR_CULL_PUNCT      2u   // general category P*
#define ANOSTR_CULL_MARK       4u   // combining marks M* (accents, Zalgo)

// s minus every rune whose class is in `classes` (any ANOSTR_CULL_* bits).
// No match returns s unchanged. Empty string on allocation failure.
anostr_t anostr_cull(mi_heap_t *heap, anostr_t s, uint32_t classes);

// The runes of s sorted ascending by code point (malformed bytes decode as U+FFFD).
// Empty string on allocation failure.
anostr_t anostr_rune_sort(mi_heap_t *heap, anostr_t s);

/* Encoding conversion */

// For foreign boundaries (Win32 W APIs, middleware).
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
