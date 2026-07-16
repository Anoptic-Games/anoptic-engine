/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic String API

// anostr_t: 16-byte immutable Umbra/German-string value.
// len <= 12: inline in the value. len > 12: backing pointer + 4-byte prefix cache.
// Byte-transparent, not NUL-terminated. Use anostr_to_cstr() for C strings.

#ifndef ANOPTICENGINE_ANOPTIC_STRINGS_H
#define ANOPTICENGINE_ANOPTIC_STRINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_memory.h"


/* Value Type */

// Layout (16 bytes, both variants; prefix at the same offset either way):

//         0        4        8                16
//         ├────────┼────────┼────────────────┤
// SHORT   │ len    │ prefix │ suffix[8]      │   len <= 12: bytes are prefix ++ suffix,
//         ├────────┼────────┼────────────────┤   contiguous at offset 4.
// LONG    │ len    │ prefix │ const char* ───┼─► len > 12: all len bytes in backing (heap or borrow);
//         └────────┴────────┴────────────────┘   prefix caches bytes[0..4).

// Invariants (constructors enforce):
//   I1  len <= UINT32_MAX; longer input -> empty.
//   I2  len <= 12 <=> inline (canonical form).
//   I3  Bytes past len are 0x00 (bit-identical eq; truthful bswapped-prefix order).
//   I4  Long bytes live as long as their backing (heap or external borrow). Inline forever.
//       anostr_bytes(&inline) points into *s -- valid only while that object lives at that address.
//   I5  Frozen values are immutable.

#define ANOSTR_INLINE_CAP 12u

typedef struct anostr_t {
    uint32_t len;               // byte count
    char     prefix[4];         // first min(len,4) bytes, 0x00-padded
    union {
        char        suffix[8];  // len <= 12: bytes [4..len), 0x00-padded
        const char* ptr;        // len >  12: all len bytes in backing (heap or borrow)
    };
} anostr_t;

static_assert(sizeof(anostr_t) == 16, "anostr_t must be a 16-byte value");
static_assert(offsetof(anostr_t, prefix) == 4 && offsetof(anostr_t, suffix) == 8,
              "inline bytes must be contiguous at offset 4");


/* Construction */

// All total: bad input yields the empty string, never UB.

// Empty string: inline, len 0, all zero.
static inline anostr_t anostr_empty(void)
{
    anostr_t s = {0};
    return s;
}

// Copy len bytes: inline if len <= 12, else from heap. Empty on bad input or alloc fail.
anostr_t anostr_from(mi_heap_t *heap, const void *bytes, size_t len);

// anostr_from over strlen(cstr). NULL -> empty.
anostr_t anostr_from_cstr(mi_heap_t *heap, const char *cstr);

// Wrap bytes without copy. len <= 12 copies inline; len > 12 borrows (bytes outlive every use; never mutate underneath).
// For literals use anostr_lit.
anostr_t anostr_view(const char *bytes, size_t len);

// Value from a string literal (static storage; borrow is sound).
#define anostr_lit(strlit) anostr_view("" strlit, sizeof(strlit) - 1)


/* Accessors */

static inline size_t anostr_len(anostr_t s)       { return s.len; }
static inline bool   anostr_is_empty(anostr_t s)  { return s.len == 0; }
static inline bool   anostr_is_inline(anostr_t s) { return s.len <= ANOSTR_INLINE_CAP; }

// Bytes, NOT NUL-terminated; read exactly anostr_len(*s).
// Inline: into *s (valid while *s lives at this address). Long: into backing (heap or borrow).
static inline const char *anostr_bytes(const anostr_t *s)
{
    return s->len <= ANOSTR_INLINE_CAP ? s->prefix : s->ptr;
}

// printf "%.*s" adapter -- no anostr_to_cstr copy:
//     ano_log(ANO_INFO, "loading %.*s", anostr_fmt(path));
#define anostr_fmt(s) (int)anostr_len((s)), anostr_bytes((const anostr_t[]){ (s) })


/* Comparison */

// Lexicographic over bytes (memcmp order); proper prefix sorts first.
// Fast path: len+prefix in-register. Prefix as big-endian u32 so integer < is byte-lexicographic.
static inline uint32_t anostr_prefix_key_(anostr_t s)
{
    uint32_t p;
    memcpy(&p, s.prefix, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    p = __builtin_bswap32(p);
#endif
    return p;
}

static inline bool anostr_eq(anostr_t a, anostr_t b)
{
    // First 8 bytes are len+prefix in both variants.
    uint64_t ha, hb;
    memcpy(&ha, &a, 8);
    memcpy(&hb, &b, 8);
    if (ha != hb)
        return false;
    if (a.len <= ANOSTR_INLINE_CAP) {           // same len => same variant
        uint64_t ta, tb;                        // equal inline strings bit-identical
        memcpy(&ta, a.suffix, 8);
        memcpy(&tb, b.suffix, 8);
        return ta == tb;
    }
    // Shared backing equals without a read.
    return a.ptr == b.ptr || memcmp(a.ptr, b.ptr, a.len) == 0;
}

// < 0, 0, > 0 like memcmp. Bytes lexicographic, then shorter-first.
static inline int anostr_compare(anostr_t a, anostr_t b)
{
    uint32_t pa = anostr_prefix_key_(a), pb = anostr_prefix_key_(b);
    if (pa != pb)
        return pa < pb ? -1 : 1;                // in-register, no dereference
    size_t n = a.len < b.len ? a.len : b.len;
    int c = memcmp(anostr_bytes(&a), anostr_bytes(&b), n);
    if (c != 0)
        return c < 0 ? -1 : 1;
    return a.len == b.len ? 0 : (a.len < b.len ? -1 : 1);
}

// FNV-1a 64 over the bytes. For tables/dedup. Equal strings hash equal across variant/heap.
// Runtime twin of ANOSTR_SID: anostr_hash(anostr_lit(x)) == ANOSTR_SID(x).
uint64_t anostr_hash(anostr_t s);

// FNV-1a 32. Runtime twin of ANOSTR_SID32.
uint32_t anostr_hash32(anostr_t s);


/* Slicing and Promotion */

// Sub-string [start, end) -- clamped, total, allocation-free. <= 12: fresh inline; longer borrows s's backing (same lifetime as s).
anostr_t anostr_slice(anostr_t s, size_t start, size_t end);

// Promote s to live as long as heap. Inline: identity. Long: copy into heap. heap is calling-thread owned (mimalloc single-writer). Empty on alloc fail.
anostr_t anostr_keep(mi_heap_t *heap, anostr_t s);

// NUL-terminated copy from heap (len + 0x00). NULL on alloc fail. Embedded 0x00 truncates the C view.
char *anostr_to_cstr(mi_heap_t *heap, anostr_t s);


/* Search, Join, Split */

// Byte-level. <= 12: fresh inline; longer: borrow source (split) or live in heap (concat/join/replace).

#define ANOSTR_NPOS SIZE_MAX

// Byte index of first needle at/after `from`; ANOSTR_NPOS if absent. Empty needle -> min(from, len).
size_t anostr_find(anostr_t s, anostr_t needle, size_t from);

// Every needle replaced by repl, LTR, non-overlapping. Empty needle or zero matches -> s unchanged. Empty on overflow/alloc fail.
anostr_t anostr_replace_all(mi_heap_t *heap, anostr_t s, anostr_t needle, anostr_t repl);

// a ++ b. Allocates only when result exceeds inline cap. Empty on overflow/alloc fail.
anostr_t anostr_concat(mi_heap_t *heap, anostr_t a, anostr_t b);

// parts joined by sep; count == 0 -> empty. One exact-size alloc (none if fits inline). Empty on fail.
anostr_t anostr_join(mi_heap_t *heap, anostr_t sep, const anostr_t *parts, size_t count);

// Zero-alloc splitter. Empty pieces included. Pieces re-canonicalize per anostr_slice. Empty sep -> s whole.
typedef struct anostr_split_t {
    anostr_t src, sep;  // long-piece lifetimes follow src's backing
    size_t   pos;
    bool     done;
} anostr_split_t;

static inline anostr_split_t anostr_split(anostr_t s, anostr_t sep)
{
    anostr_split_t it = { .src = s, .sep = sep, .pos = 0, .done = false };
    return it;
}

// Next piece -> true; false once exhausted (piece untouched).
bool anostr_split_next(anostr_split_t *it, anostr_t *piece);


/* Interning */

// Dedupe + dense u32 identity. One owner thread mutates. No destroy -- dies with heap.
// Runtime identity table; ANOSTR_SID is the compile-time sibling.
typedef uint32_t anostr_sym;
#define ANOSTR_SYM_NONE UINT32_MAX

typedef struct anostr_intern_t anostr_intern_t;

// Table allocating from heap (heap outlives the table). NULL on alloc fail.
anostr_intern_t *anostr_intern_make(mi_heap_t *heap);

// Symbol for s's contents, inserting a canonical copy on first sight. ANOSTR_SYM_NONE on alloc fail.
anostr_sym anostr_intern(anostr_intern_t *t, anostr_t s);

// Lookup without insertion: symbol, or ANOSTR_SYM_NONE.
anostr_sym anostr_intern_find(const anostr_intern_t *t, anostr_t s);

// Canonical value for a symbol (lives as long as the table's heap). Empty for NONE/out-of-range.
anostr_t anostr_sym_str(const anostr_intern_t *t, anostr_sym sym);

// Intern s, return the canonical value. Equal inputs -> bit-identical (shared backing). Fail -> s.
anostr_t anostr_dedupe(anostr_intern_t *t, anostr_t s);

// Distinct strings interned; symbols are dense 0 .. count-1.
size_t anostr_intern_count(const anostr_intern_t *t);


/* Compile-time String IDs */

// FNV-1a ICE from a literal. Usable as case/enum/static/array size.
// ANOSTR_SID(x) == anostr_hash(anostr_lit(x)); likewise SID32/hash32.
// Bytes = sizeof - 1 (embedded NULs count). Literals > ANOSTR_SID_MAX fail to compile.
typedef uint64_t anostr_sid;
typedef uint32_t anostr_sid32;

#define ANOSTR_SID_MAX 128u

// One FNV-1a step, masked to width; identity past the literal's last byte.
#define ANOSTR_SID_B_(s, i) ((i) < sizeof(s) - 1 ? (uint64_t)(uint8_t)(s)[i] : UINT64_C(0))
#define ANOSTR_SID_1_(s, i, h, p, m) \
    ((((h) ^ ANOSTR_SID_B_(s, i)) * ((i) < sizeof(s) - 1 ? (p) : UINT64_C(1))) & (m))
// Unroll tiers: 4, 16, 64, then ANOSTR_SID_MAX bytes.
#define ANOSTR_SID_4_(s, i, h, p, m) \
    ANOSTR_SID_1_(s, (i) + 3, ANOSTR_SID_1_(s, (i) + 2, \
    ANOSTR_SID_1_(s, (i) + 1, ANOSTR_SID_1_(s, i, h, p, m), p, m), p, m), p, m)
#define ANOSTR_SID_16_(s, i, h, p, m) \
    ANOSTR_SID_4_(s, (i) + 12, ANOSTR_SID_4_(s, (i) + 8, \
    ANOSTR_SID_4_(s, (i) + 4, ANOSTR_SID_4_(s, i, h, p, m), p, m), p, m), p, m)
#define ANOSTR_SID_64_(s, i, h, p, m) \
    ANOSTR_SID_16_(s, (i) + 48, ANOSTR_SID_16_(s, (i) + 32, \
    ANOSTR_SID_16_(s, (i) + 16, ANOSTR_SID_16_(s, i, h, p, m), p, m), p, m), p, m)
#define ANOSTR_SID_ALL_(s, h, p, m) \
    ANOSTR_SID_64_(s, 64u, ANOSTR_SID_64_(s, 0u, h, p, m), p, m)
// Overlong literal: negative array size -> compile error.
#define ANOSTR_SID_GUARD_(s) (0 * sizeof(char[1 - 2 * (sizeof(s) - 1 > ANOSTR_SID_MAX)]))

#define ANOSTR_SID(strlit)                                                        \
    ((anostr_sid)(ANOSTR_SID_ALL_("" strlit, UINT64_C(0xcbf29ce484222325),        \
                                  UINT64_C(0x100000001b3), UINT64_MAX)            \
                  + ANOSTR_SID_GUARD_("" strlit)))

#define ANOSTR_SID32(strlit)                                                      \
    ((anostr_sid32)(ANOSTR_SID_ALL_("" strlit, UINT64_C(0x811c9dc5),              \
                                    UINT64_C(0x01000193), UINT64_C(0xffffffff))   \
                    + ANOSTR_SID_GUARD_("" strlit)))


/* Builder */

// The ONLY mutation path. Accumulate in scratch, freeze to an immutable value.
// Geometric growth. Not thread-safe; one owner.
typedef struct anostr_builder_t {
    char      *ptr;   // heap-owned, cap bytes; NULL until allocated (reserve 0 defers)
    uint32_t   len;
    uint32_t   cap;
    mi_heap_t *heap;  // NULL = consumed/discarded: appends fail with -1
} anostr_builder_t;

// Builder from heap, cap >= reserve (0 defers allocation). heap non-NULL, calling-thread-owned.
anostr_builder_t anostr_builder_make(mi_heap_t *heap, uint32_t reserve);

// Append n raw bytes. 0, or -1 on consumed/overflow/growth fail (builder intact on fail).
int anostr_builder_append(anostr_builder_t *b, const void *bytes, size_t n);

int anostr_builder_append_str(anostr_builder_t *b, anostr_t s);

// Append a NUL-terminated C string. Same returns as anostr_builder_append.
int anostr_builder_append_cstr(anostr_builder_t *b, const char *cstr);

// printf into the builder (grows to fit). Same returns as anostr_builder_append.
int anostr_builder_appendf(anostr_builder_t *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Consume builder -> immutable value. <= 12: inline, buffer freed. Longer: shrink-to-len, value takes buffer.
// Builder zeroed; further appends return -1.
anostr_t anostr_freeze(anostr_builder_t *b);

// Consume without a value; frees the buffer now.
void anostr_builder_discard(anostr_builder_t *b);

#endif //ANOPTICENGINE_ANOPTIC_STRINGS_H
