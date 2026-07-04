/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic String API
//
// One public string type: anostr_t, a 16-byte immutable value (Umbra "German string" layout).
// Strings of <= 12 bytes live entirely inside the value and exist on the stack.
// Longer strings point into an arena passed in by caller, 
// the value caches the first 4 bytes so most compares resolve without touching the heap.
//
// Storage is byte-transparent. anostr_t is a byte array and is not NUL-terminated. 
// Use anostr_to_cstr() if you need a NUL-terminated string.

#ifndef ANOPTICENGINE_ANOPTIC_STRINGS_H
#define ANOPTICENGINE_ANOPTIC_STRINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_memory.h"

// ---------------------------------------------------------------------------------------------
// The value type.
//
// Layout (16 bytes, both variants; prefix sits at the same offset either way):
//
//         0        4        8                16
//         ├────────┼────────┼────────────────┤
// SHORT   │ len    │ prefix │ suffix[8]      │   len <= 12: bytes are prefix ++ suffix,
//         ├────────┼────────┼────────────────┤   contiguous at offset 4.
// LONG    │ len    │ prefix │ const char* ───┼─► len > 12: all len bytes live in some heap;
//         └────────┴────────┴────────────────┘   prefix caches bytes[0..4).
//
// Invariants (stated in machine arithmetic, checked by constructors):
//   I1  len <= UINT32_MAX. Constructors reject longer input (return the empty string).
//   I2  Canonical form: len <= 12 <=> inline. No long value ever holds <= 12 bytes, so two
//       equal strings always share a variant.
//   I3  Inline bytes beyond len are 0x00, and prefix pads with 0x00 past len for len < 4.
//       Load-bearing: it makes equal inline strings bit-identical (eq = two u64 compares)
//       and makes the bswapped-prefix ordering truthful even with embedded 0x00 bytes.
//   I4  A long value's bytes are valid exactly as long as the heap they were built in.
//       An inline value is valid forever. anostr_bytes(&s) for an inline value points INTO
//       s itself -- valid only while that particular object is alive at that address.
//   I5  Frozen values are immutable. Nothing in this API writes through anostr_t.ptr.

#define ANOSTR_INLINE_CAP 12u

typedef struct anostr_t {
    uint32_t len;               // byte count
    char     prefix[4];         // first min(len,4) bytes, 0x00-padded
    union {
        char        suffix[8];  // len <= 12: bytes [4..len), 0x00-padded
        const char* ptr;        // len >  12: all len bytes, arena-owned
    };
} anostr_t;

static_assert(sizeof(anostr_t) == 16, "anostr_t must be a 16-byte value");
static_assert(offsetof(anostr_t, prefix) == 4 && offsetof(anostr_t, suffix) == 8,
              "inline bytes must be contiguous at offset 4");

// ---------------------------------------------------------------------------------------------
// Construction. All total: bad input yields the empty string, never UB.

// The empty string: inline, len 0, all zero.
static inline anostr_t anostr_empty(void)
{
    anostr_t s = {0};
    return s;
}

// Copy len bytes into a value: inline if len <= 12, else allocated from heap.
// Returns the empty string if len > UINT32_MAX, bytes is NULL, or the heap allocation fails.
anostr_t anostr_from(mi_heap_t *heap, const void *bytes, size_t len);

// anostr_from over strlen(cstr). cstr must be NUL-terminated; NULL yields the empty string.
anostr_t anostr_from_cstr(mi_heap_t *heap, const char *cstr);

// Wrap existing bytes WITHOUT copying (no allocation ever). 
// len <= 12 copies inline (and then owes nothing to bytes); 
// len > 12 borrows: the caller guarantees bytes outlives every use of the value and never mutates underneath it.
// For string literals use anostr_lit.
anostr_t anostr_view(const char *bytes, size_t len);

// A value from a string literal. Static storage, so the borrow in anostr_view is always sound.
#define anostr_lit(strlit) anostr_view("" strlit, sizeof(strlit) - 1)

// ---------------------------------------------------------------------------------------------
// Accessors.

static inline size_t anostr_len(anostr_t s)       { return s.len; }
static inline bool   anostr_is_empty(anostr_t s)  { return s.len == 0; }
static inline bool   anostr_is_inline(anostr_t s) { return s.len <= ANOSTR_INLINE_CAP; }

// The string's bytes, NOT NUL-terminated; read exactly anostr_len(*s) of them.
// Inline: points into *s (valid while *s lives at this address). 
// Long: points into the backing heap.
static inline const char *anostr_bytes(const anostr_t *s)
{
    return s->len <= ANOSTR_INLINE_CAP ? s->prefix : s->ptr;
}

// Format-argument adapter: pass a value to any printf-family "%.*s" conversion, the logger included -- no anostr_to_cstr copy:
//     ano_log(ANO_INFO, "loading %.*s", anostr_fmt(path));
#define anostr_fmt(s) (int)anostr_len((s)), anostr_bytes((const anostr_t[]){ (s) })

// ---------------------------------------------------------------------------------------------
// Comparison. Lexicographic over bytes (memcmp order); a proper prefix sorts first.
// The fast path never dereferences: len+prefix answer most real-world compares in-register.
// Prefix as a big-endian u32 so integer < is byte-lexicographic.
// The prefix is stored in big-endian order to make the byte-lexicographic order truthful even with embedded 0x00 bytes.
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
    // First 8 bytes are len+prefix in both variants; unequal there is unequal, full stop.
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
    // Shared backing (slices of one string, deduped/interned values) equals without a read.
    return a.ptr == b.ptr || memcmp(a.ptr, b.ptr, a.len) == 0;
}

// < 0, 0, > 0 like memcmp. 
// Total order: bytes lexicographic, then shorter-first.
static inline int anostr_compare(anostr_t a, anostr_t b)
{
    uint32_t pa = anostr_prefix_key_(a), pb = anostr_prefix_key_(b);
    if (pa != pb)
        return pa < pb ? -1 : 1;                // answered in-register, no dereference
    size_t n = a.len < b.len ? a.len : b.len;
    int c = memcmp(anostr_bytes(&a), anostr_bytes(&b), n);
    if (c != 0)
        return c < 0 ? -1 : 1;
    return a.len == b.len ? 0 : (a.len < b.len ? -1 : 1);
}

// FNV-1a 64 over the bytes. 
// Not cryptographic; for tables and dedup. 
// Equal strings hash equal regardless of variant or backing heap.
uint64_t anostr_hash(anostr_t s);

// ---------------------------------------------------------------------------------------------
// Slicing and promotion.

// Sub-string [start, end) -- clamped: end = min(end, len), start = min(start, end), so every call is total and allocation-free. 
// A slice of <= 12 bytes becomes a fresh inline value; a longer slice of a long string borrows the same backing bytes (same lifetime as s).
anostr_t anostr_slice(anostr_t s, size_t start, size_t end);

// Promote s so it lives as long as heap.
// Inline values return identically (nothing to own); long values copy their bytes into heap.
// heap must be owned by the calling thread (mimalloc heaps are single-writer). 
// Returns the empty string if the allocation fails.
anostr_t anostr_keep(mi_heap_t *heap, anostr_t s);

// NUL-terminated copy for C APIs, allocated from heap: len bytes + one 0x00 terminator, valid as long as heap. 
// NULL on allocation failure. Embedded 0x00 bytes will truncate the C view.
char *anostr_to_cstr(mi_heap_t *heap, anostr_t s);

// ---------------------------------------------------------------------------------------------
// Search, join, split. The byte-level toolkit. 
// Results are values under the same rules as everything above: <= 12 bytes is a fresh inline value; longer results either borrow their source (split pieces) or live in the heap you passed (concat/join).

#define ANOSTR_NPOS SIZE_MAX

// Byte index of the first occurrence of needle at or after `from`; ANOSTR_NPOS if absent.
// An empty needle matches immediately at min(from, len). Total: any `from` is safe.
size_t anostr_find(anostr_t s, anostr_t needle, size_t from);

// Every needle replaced by repl, left to right, non-overlapping ("aaa"/"aa" once).
// Byte-level: UTF-8 self-synchronizes, so matches land on rune boundaries. 
// Zero matches or empty needle return s unchanged. 
// Empty string if the result exceeds UINT32_MAX or allocation fails.
anostr_t anostr_replace_all(mi_heap_t *heap, anostr_t s, anostr_t needle, anostr_t repl);

// a ++ b. Allocates from heap only when the result exceeds the inline cap. 
// Returns the empty string if the total exceeds UINT32_MAX or allocation fails.
anostr_t anostr_concat(mi_heap_t *heap, anostr_t a, anostr_t b);

// parts[0] sep parts[1] sep ... sep parts[count - 1]; count == 0 yields the empty string.
// One exact-size allocation (none if the total fits inline). 
// Returns the empty string if parts is NULL, the total exceeds UINT32_MAX, or allocation fails.
anostr_t anostr_join(mi_heap_t *heap, anostr_t sep, const anostr_t *parts, size_t count);

// Zero-allocation splitter. Yields every piece between separators, empty pieces included:
// "a,,b" on "," yields "a", "", "b"; a trailing separator yields a trailing empty piece.
// Pieces re-canonicalize per anostr_slice: <= 12 bytes copy inline, longer pieces borrow s's backing bytes and share its lifetime.
// An empty sep yields s whole.
typedef struct anostr_split_t {
    anostr_t src, sep;  // iterated by value; long-piece lifetimes follow src's backing
    size_t   pos;
    bool     done;
} anostr_split_t;

static inline anostr_split_t anostr_split(anostr_t s, anostr_t sep)
{
    anostr_split_t it = { .src = s, .sep = sep, .pos = 0, .done = false };
    return it;
}

// Writes the next piece and returns true; false once exhausted (piece untouched).
bool anostr_split_next(anostr_split_t *it, anostr_t *piece);

// ---------------------------------------------------------------------------------------------
// Interning: dedupe + i    nteger identity.
// One canonical copy of each distinct string lives in the table's heap; a symbol is a dense u32 (0 .. count-1) you compare and switch on. 
// Same threading rule as the heap underneath: one owner thread mutates (intern/dedupe); concurrent readers need external ordering.
// No destroy function on purpose -- the table and every canonical byte die with the heap.
// This is the RUNTIME identity table; compile-time _sid hashes are a separate primitive.
typedef uint32_t anostr_sym;
#define ANOSTR_SYM_NONE UINT32_MAX

typedef struct anostr_intern_t anostr_intern_t;

// A table allocating from heap (which must outlive it). 
// NULL on allocation failure.
anostr_intern_t *anostr_intern_make(mi_heap_t *heap);

// The symbol for s's contents, inserting a canonical copy on first sight. 
// Stable for the table's lifetime; equal strings always map to the same symbol, regardless of variant or backing.
// ANOSTR_SYM_NONE on allocation failure.
anostr_sym anostr_intern(anostr_intern_t *t, anostr_t s);

// Lookup without insertion: the symbol, or ANOSTR_SYM_NONE if never interned.
anostr_sym anostr_intern_find(const anostr_intern_t *t, anostr_t s);

// The canonical value for a symbol, valid as long as the table's heap. 
// The empty string for ANOSTR_SYM_NONE or an out-of-range symbol.
anostr_t anostr_sym_str(const anostr_intern_t *t, anostr_sym sym);

// Dedupe face: intern s and return the canonical value. 
// Equal inputs return bit-identical values (same backing pointer), so anostr_eq on deduped strings never reads the bytes.
anostr_t anostr_dedupe(anostr_intern_t *t, anostr_t s);

// Distinct strings interned so far; symbols are dense 0 .. count-1.
size_t anostr_intern_count(const anostr_intern_t *t);

// ---------------------------------------------------------------------------------------------
// The builder: the ONLY mutation path. 
// Accumulate in scratch, then freeze to an immutable    value. 
// Growth is geometric (cap <= 2 * final len). 
// Not thread-safe; one owner.
typedef struct anostr_builder_t {
    char      *ptr;   // heap-owned, cap bytes; NULL until first append (or reserve = 0)
    uint32_t   len;
    uint32_t   cap;
    mi_heap_t *heap;  // NULL marks a consumed/discarded builder: appends fail with -1
} anostr_builder_t;

// A builder allocating from heap, with cap >= reserve up front (reserve 0 defers allocation).
// heap must be non-NULL and owned by the calling thread.
anostr_builder_t anostr_builder_make(mi_heap_t *heap, uint32_t reserve);

// Append n raw bytes. 
// Returns 0, or -1 if the builder is consumed, len + n > UINT32_MAX, or growth fails (the builder is left intact in every failure case).
int anostr_builder_append(anostr_builder_t *b, const void *bytes, size_t n);

int anostr_builder_append_str(anostr_builder_t *b, anostr_t s);

// Append a NUL-terminated C string. Same returns as anostr_builder_append.
int anostr_builder_append_cstr(anostr_builder_t *b, const char *cstr);

// printf into the builder (grows to fit exactly). Same returns as anostr_builder_append.
int anostr_builder_appendf(anostr_builder_t *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Consume the builder, returning the immutable value. 
// <= 12 bytes: inline value, the buffer is freed. 
// Longer: the buffer is shrunk to exactly len and the value takes it over (lives as long as the builder's heap)
// The builder is zeroed; further appends return -1.
anostr_t anostr_freeze(anostr_builder_t *b);

// Consume the builder without producing a value; frees the buffer now rather than at heap teardown. 
// For abandoned construction in long-lived heaps.
void anostr_builder_discard(anostr_builder_t *b);

#endif //ANOPTICENGINE_ANOPTIC_STRINGS_H
