/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_strings.h -- the 16-byte German-string value, the builder, and the
 * lifetime ceremony:
 *   - canonical form (I2) and zero padding (I3): equal strings are bit-identical inline;
 *   - construction round-trips at every length 0..64, inline and heap-backed;
 *   - eq / compare / hash: lexicographic order, embedded 0x00 bytes, variant independence;
 *   - slice: clamping, copy-on-shrink to inline, borrow (same backing pointer) for long slices;
 *   - keep: identity on inline values, real copy off a scratch heap that is then destroyed;
 *   - builder: append/appendf growth, freeze to both variants, consumed-builder failure,
 *     discard, UINT32_MAX overflow refusal;
 *   - find / concat / join: offsets, absence, empty needles, inline vs heap results,
 *     separator placement (a path join among them);
 *   - split: empty pieces, trailing separator, no separator, empty separator, borrow
 *     semantics for long pieces;
 *   - intern/dedupe: symbol stability across variants and allocations, find-without-insert,
 *     sym_str round-trip, bit-identical dedupe, growth past the initial slot table;
 *   - ANOSTR_SID / ANOSTR_SID32: published FNV-1a vectors as static_asserts, ICE contexts
 *     (case label, enum, static initializer, array size), runtime-twin agreement with
 *     anostr_hash/anostr_hash32 (embedded NUL and the 128-byte cap included);
 *   - a randomized round-trip soak (fixed seed; argv[1] scales iterations).
 * Exit 0 == pass; failures print what broke. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_strings.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Contents match, byte for byte, at the exact advertised length.
static bool str_equals_mem(anostr_t s, const void *bytes, size_t len)
{
    return anostr_len(s) == len && memcmp(anostr_bytes(&s), bytes, len) == 0;
}

static void test_construction_roundtrip(mi_heap_t *heap)
{
    char src[65];
    for (size_t i = 0; i < 64; i++) src[i] = (char)('a' + i % 26);
    src[64] = '\0';

    for (size_t len = 0; len <= 64; len++) {
        anostr_t s = anostr_from(heap, src, len);
        CHECK(str_equals_mem(s, src, len), "anostr_from round-trips contents");
        CHECK(anostr_is_inline(s) == (len <= ANOSTR_INLINE_CAP),
              "canonical form: inline iff len <= 12 (I2)");
    }

    anostr_t empty = anostr_from(heap, src, 0);
    CHECK(anostr_is_empty(empty), "zero-length string is empty");
    CHECK(anostr_eq(empty, anostr_empty()), "empty equals anostr_empty()");

    CHECK(anostr_is_empty(anostr_from(heap, NULL, 5)), "NULL bytes yield the empty string");
    CHECK(anostr_is_empty(anostr_from(NULL, src, 32)), "NULL heap on a long string yields empty");
    CHECK(str_equals_mem(anostr_from(NULL, src, 8), src, 8),
          "NULL heap is fine for an inline string (no allocation)");

    anostr_t lit = anostr_lit("hello");
    CHECK(str_equals_mem(lit, "hello", 5), "anostr_lit round-trips");
    anostr_t cs = anostr_from_cstr(heap, "categorically");     // 13 bytes -> long
    CHECK(str_equals_mem(cs, "categorically", 13), "anostr_from_cstr round-trips long");
}

static void test_padding_makes_equals_bitwise(mi_heap_t *heap)
{
    // I3: the same short string built three different ways must be bit-identical.
    anostr_t a = anostr_lit("tick");
    anostr_t b = anostr_from(heap, "tick", 4);
    anostr_t c = anostr_slice(anostr_lit("[tick]"), 1, 5);
    CHECK(memcmp(&a, &b, sizeof(anostr_t)) == 0, "inline equal strings bit-identical (lit vs from)");
    CHECK(memcmp(&a, &c, sizeof(anostr_t)) == 0, "inline equal strings bit-identical (vs slice)");
}

static void test_eq_compare_hash(mi_heap_t *heap)
{
    // Inline order and equality.
    CHECK(anostr_eq(anostr_lit("abc"), anostr_lit("abc")), "eq: identical inline");
    CHECK(!anostr_eq(anostr_lit("abc"), anostr_lit("abd")), "eq: differing inline");
    CHECK(anostr_compare(anostr_lit("abc"), anostr_lit("abd")) < 0, "compare: abc < abd");
    CHECK(anostr_compare(anostr_lit("ab"), anostr_lit("abc")) < 0, "compare: proper prefix first");
    CHECK(anostr_compare(anostr_lit(""), anostr_lit("a")) < 0, "compare: empty sorts first");
    CHECK(anostr_compare(anostr_lit("abc"), anostr_lit("abc")) == 0, "compare: equal is 0");

    // Embedded 0x00: byte-transparent storage must order and compare them like memcmp.
    anostr_t n1 = anostr_from(heap, "a\0b", 3);
    anostr_t n2 = anostr_from(heap, "a\0c", 3);
    CHECK(!anostr_eq(n1, n2), "eq sees past an embedded NUL");
    CHECK(anostr_compare(n1, n2) < 0, "compare orders past an embedded NUL");

    // Long strings: equal contents in different allocations must be equal; the last byte
    // must be reachable by eq/compare (the prefix alone must not declare victory).
    const char *base = "shared-prefix-long-string-A";
    const char *diff = "shared-prefix-long-string-B";
    size_t n = strlen(base);
    anostr_t l1 = anostr_from(heap, base, n);
    anostr_t l2 = anostr_from(heap, base, n);
    anostr_t l3 = anostr_from(heap, diff, n);
    CHECK(anostr_bytes(&l1) != anostr_bytes(&l2), "distinct allocations for the eq test");
    CHECK(anostr_eq(l1, l2), "eq: equal long strings across allocations");
    CHECK(!anostr_eq(l1, l3), "eq: long strings differing at the last byte");
    CHECK(anostr_compare(l1, l3) < 0 && anostr_compare(l3, l1) > 0,
          "compare: long strings differing past the prefix");

    // Different lengths can never be equal (canonical form makes cross-variant eq a len test).
    CHECK(!anostr_eq(anostr_lit("short"), l1), "eq: inline vs long is never equal");

    // Hash: variant- and allocation-independent, and FNV-1a actually mixes.
    CHECK(anostr_hash(l1) == anostr_hash(l2), "hash equal across allocations");
    CHECK(anostr_hash(anostr_lit("tick")) == anostr_hash(anostr_from(heap, "tick", 4)),
          "hash equal across construction paths");
    CHECK(anostr_hash(anostr_lit("tick")) != anostr_hash(anostr_lit("tock")),
          "hash differs for differing strings");
    CHECK(anostr_hash(n1) != anostr_hash(n2), "hash sees past an embedded NUL");
}

static void test_slice(mi_heap_t *heap)
{
    const char *text = "the quick brown fox jumps over the lazy dog";   // 43 bytes
    anostr_t s = anostr_from(heap, text, strlen(text));

    anostr_t word = anostr_slice(s, 4, 9);                  // "quick" -> inline copy
    CHECK(str_equals_mem(word, "quick", 5), "short slice contents");
    CHECK(anostr_is_inline(word), "short slice re-canonicalizes to inline (I2)");

    anostr_t tail = anostr_slice(s, 10, anostr_len(s));     // 33 bytes -> borrow
    CHECK(str_equals_mem(tail, text + 10, 33), "long slice contents");
    CHECK(!anostr_is_inline(tail), "long slice stays long");
    CHECK(anostr_bytes(&tail) == anostr_bytes(&s) + 10,
          "long slice borrows the same backing bytes (no copy)");

    CHECK(anostr_eq(anostr_slice(s, 0, anostr_len(s)), s), "full slice equals the source");
    CHECK(anostr_is_empty(anostr_slice(s, 7, 7)), "empty range yields empty");
    CHECK(str_equals_mem(anostr_slice(s, 40, 999), text + 40, 3), "end clamps to len");
    CHECK(anostr_is_empty(anostr_slice(s, 999, 1000)), "start past len clamps to empty");

    anostr_t in = anostr_lit("inline-str");
    CHECK(str_equals_mem(anostr_slice(in, 7, 10), "str", 3), "slice of an inline string");
}

static void test_keep(mi_heap_t *persistent)
{
    // Inline: keep is identity, no heap touched (NULL heap must be safe).
    anostr_t v = anostr_lit("value");
    anostr_t kept = anostr_keep(NULL, v);
    CHECK(memcmp(&v, &kept, sizeof(anostr_t)) == 0, "keep on inline is identity");

    // Long: build in a scratch heap, keep into the persistent heap, destroy scratch,
    // then prove the kept copy survived with its own backing.
    anostr_t survivor;
    const char *msg = "outlives-the-scratch-heap-it-was-born-in";
    {
        mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
        CHECK(scratch != NULL, "scratch heap created");
        anostr_t born = anostr_from(scratch, msg, strlen(msg));
        survivor = anostr_keep(persistent, born);
        CHECK(anostr_bytes(&survivor) != anostr_bytes(&born), "keep copied the long bytes");
    }   // scratch heap destroyed here
    CHECK(str_equals_mem(survivor, msg, strlen(msg)), "kept string readable after scratch died");

    char *cstr = anostr_to_cstr(persistent, survivor);
    CHECK(cstr != NULL && strlen(cstr) == anostr_len(survivor) && strcmp(cstr, msg) == 0,
          "to_cstr round-trips with a NUL terminator");
}

static void test_builder(mi_heap_t *heap)
{
    // Freeze to inline: short accumulations produce a value owing nothing to the heap.
    anostr_builder_t b = anostr_builder_make(heap, 0);
    CHECK(anostr_builder_append_cstr(&b, "tick=") == 0, "append_cstr");
    CHECK(anostr_builder_appendf(&b, "%d", 42) == 0, "appendf");
    anostr_t shortStr = anostr_freeze(&b);
    CHECK(str_equals_mem(shortStr, "tick=42", 7), "builder freeze contents (short)");
    CHECK(anostr_is_inline(shortStr), "short freeze canonicalizes to inline (I2)");
    CHECK(anostr_builder_append_cstr(&b, "x") == -1, "consumed builder refuses appends");
    CHECK(anostr_is_empty(anostr_freeze(&b)), "consumed builder freezes to empty");

    // Freeze to long, growth across many appends, all append flavors.
    anostr_builder_t big = anostr_builder_make(heap, 8);    // deliberately small reserve
    for (int i = 0; i < 100; i++)
        CHECK(anostr_builder_appendf(&big, "entity_%03d;", i) == 0, "appendf in a loop");
    CHECK(anostr_builder_append_str(&big, anostr_lit("done")) == 0, "append_str");
    anostr_t longStr = anostr_freeze(&big);
    CHECK(anostr_len(longStr) == 100 * 11 + 4, "builder length adds up");
    CHECK(!anostr_is_inline(longStr), "long freeze stays long");
    CHECK(memcmp(anostr_bytes(&longStr), "entity_000;entity_001;", 22) == 0,
          "builder freeze contents (head)");
    CHECK(memcmp(anostr_bytes(&longStr) + anostr_len(longStr) - 4, "done", 4) == 0,
          "builder freeze contents (tail)");

    // Raw bytes with an embedded NUL survive the builder byte-transparently.
    anostr_builder_t raw = anostr_builder_make(heap, 0);
    CHECK(anostr_builder_append(&raw, "a\0b", 3) == 0, "append raw bytes");
    anostr_t rawStr = anostr_freeze(&raw);
    CHECK(str_equals_mem(rawStr, "a\0b", 3), "embedded NUL survives the builder");

    // Discard frees eagerly and consumes.
    anostr_builder_t d = anostr_builder_make(heap, 64);
    CHECK(anostr_builder_append_cstr(&d, "abandoned") == 0, "append before discard");
    anostr_builder_discard(&d);
    CHECK(anostr_builder_append_cstr(&d, "x") == -1, "discarded builder refuses appends");

    // I1 ceiling: an append that would pass UINT32_MAX must refuse and leave len intact.
    anostr_builder_t of = anostr_builder_make(heap, 0);
    CHECK(anostr_builder_append_cstr(&of, "seed") == 0, "seed append");
    of.len = UINT32_MAX - 2;    // simulate a near-full builder without allocating 4 GiB
    CHECK(anostr_builder_append(&of, "xyz", 3) == -1, "overflowing append refused");
    CHECK(of.len == UINT32_MAX - 2, "failed append left the builder intact");
    of.len = 4;                 // restore truth before freeing real memory
    anostr_builder_discard(&of);
}

static void test_find_concat_join(mi_heap_t *heap)
{
    anostr_t s = anostr_from_cstr(heap, "the quick brown fox jumps over the lazy dog");

    CHECK(anostr_find(s, anostr_lit("quick"), 0) == 4, "find: first hit");
    CHECK(anostr_find(s, anostr_lit("the"), 0) == 0, "find: hit at 0");
    CHECK(anostr_find(s, anostr_lit("the"), 1) == 31, "find: from-offset skips the first hit");
    CHECK(anostr_find(s, anostr_lit("cat"), 0) == ANOSTR_NPOS, "find: absent needle");
    CHECK(anostr_find(s, anostr_lit(""), 7) == 7, "find: empty needle matches at from");
    CHECK(anostr_find(s, anostr_lit(""), 999) == anostr_len(s), "find: empty needle clamps");
    CHECK(anostr_find(anostr_lit("ab"), anostr_lit("abc"), 0) == ANOSTR_NPOS,
          "find: needle longer than haystack");
    CHECK(anostr_find(s, anostr_lit("dog"), 40) == 40, "find: hit at the very end");
    CHECK(anostr_find(s, anostr_lit("dog"), 41) == ANOSTR_NPOS, "find: from past the last hit");

    // concat: inline + inline staying inline, and crossing into a heap value.
    anostr_t ab = anostr_concat(NULL, anostr_lit("tick"), anostr_lit("=42"));
    CHECK(str_equals_mem(ab, "tick=42", 7) && anostr_is_inline(ab),
          "concat: short result is inline (no heap needed)");
    anostr_t big = anostr_concat(heap, anostr_lit("entity/1776/"), anostr_lit("hull"));
    CHECK(str_equals_mem(big, "entity/1776/hull", 16) && !anostr_is_inline(big),
          "concat: 13+ bytes goes to the heap");

    // join: separator placement, count 0/1, and a path join.
    anostr_t parts[3] = { anostr_lit("assets"), anostr_lit("models"), anostr_lit("hull.gltf") };
    anostr_t path = anostr_join(heap, anostr_lit("/"), parts, 3);
    CHECK(str_equals_mem(path, "assets/models/hull.gltf", 23), "join: path with separators");
    CHECK(anostr_eq(anostr_join(heap, anostr_lit("/"), parts, 1), parts[0]),
          "join: single part has no separator");
    CHECK(anostr_is_empty(anostr_join(heap, anostr_lit("/"), parts, 0)), "join: count 0 is empty");
    anostr_t inl = anostr_join(NULL, anostr_lit(","), parts, 1);
    CHECK(str_equals_mem(inl, "assets", 6), "join: inline result needs no heap");
}

static void test_split(mi_heap_t *heap)
{
    // Inline source: empty pieces preserved, including after a trailing separator.
    anostr_t piece;
    anostr_split_t it = anostr_split(anostr_lit("a,,b,"), anostr_lit(","));
    const char *expect[] = { "a", "", "b", "" };
    for (int i = 0; i < 4; i++) {
        CHECK(anostr_split_next(&it, &piece), "split: piece available");
        CHECK(str_equals_mem(piece, expect[i], strlen(expect[i])), "split: piece contents");
    }
    CHECK(!anostr_split_next(&it, &piece), "split: exhausted after the last piece");
    CHECK(!anostr_split_next(&it, &piece), "split: stays exhausted");

    // No separator present: the whole string, once. Same for an empty separator.
    it = anostr_split(anostr_lit("solo"), anostr_lit(","));
    CHECK(anostr_split_next(&it, &piece) && str_equals_mem(piece, "solo", 4),
          "split: no separator yields the whole string");
    CHECK(!anostr_split_next(&it, &piece), "split: then exhausted");
    it = anostr_split(anostr_lit("whole"), anostr_lit(""));
    CHECK(anostr_split_next(&it, &piece) && str_equals_mem(piece, "whole", 5),
          "split: empty separator yields the whole string once");
    CHECK(!anostr_split_next(&it, &piece), "split: empty separator then exhausted");

    // Empty source: one empty piece.
    it = anostr_split(anostr_empty(), anostr_lit(","));
    CHECK(anostr_split_next(&it, &piece) && anostr_is_empty(piece),
          "split: empty source yields one empty piece");
    CHECK(!anostr_split_next(&it, &piece), "split: empty source then exhausted");

    // Long source: multi-byte separator; long pieces borrow the source's backing (I4).
    const char *cfg = "graphics.width::graphics.height::graphics.fullscreen-mode";
    anostr_t s = anostr_from(heap, cfg, strlen(cfg));
    it = anostr_split(s, anostr_lit("::"));
    CHECK(anostr_split_next(&it, &piece) && str_equals_mem(piece, "graphics.width", 14),
          "split: multi-byte separator piece 1");
    CHECK(!anostr_is_inline(piece), "split: 13+ byte piece stays long");
    CHECK(anostr_bytes(&piece) == anostr_bytes(&s), "split: long piece borrows the source");
    CHECK(anostr_split_next(&it, &piece) && str_equals_mem(piece, "graphics.height", 15),
          "split: multi-byte separator piece 2");
    CHECK(anostr_split_next(&it, &piece)
              && str_equals_mem(piece, "graphics.fullscreen-mode", 24),
          "split: multi-byte separator piece 3");
    CHECK(!anostr_split_next(&it, &piece), "split: long source exhausted");
}

static void test_intern(mi_heap_t *heap)
{
    anostr_intern_t *t = anostr_intern_make(heap);
    CHECK(t != NULL, "intern table created");
    if (t == NULL)
        return;
    CHECK(anostr_intern_count(t) == 0, "fresh table is empty");

    // Equal strings map to one symbol regardless of construction path or variant.
    anostr_sym a = anostr_intern(t, anostr_lit("hull"));
    anostr_sym b = anostr_intern(t, anostr_from(heap, "hull", 4));
    anostr_sym c = anostr_intern(t, anostr_slice(anostr_lit("[hull]"), 1, 5));
    CHECK(a != ANOSTR_SYM_NONE, "intern returns a symbol");
    CHECK(a == b && b == c, "equal strings share one symbol");
    CHECK(anostr_intern_count(t) == 1, "one distinct string, one entry");

    const char *lp = "vulkan_backend/instance/pipelines/flat";     // a long one
    anostr_sym d = anostr_intern(t, anostr_from(heap, lp, strlen(lp)));
    anostr_sym e = anostr_intern(t, anostr_view(lp, strlen(lp)));  // other backing, same bytes
    CHECK(d != ANOSTR_SYM_NONE && d != a, "distinct strings get distinct symbols");
    CHECK(d == e, "long strings dedupe across backings");
    CHECK(str_equals_mem(anostr_sym_str(t, d), lp, strlen(lp)), "sym_str round-trips");

    // Lookup faces: find never inserts; sym_str is total.
    CHECK(anostr_intern_find(t, anostr_lit("hull")) == a, "find sees an interned string");
    CHECK(anostr_intern_find(t, anostr_lit("nope")) == ANOSTR_SYM_NONE, "find never inserts");
    CHECK(anostr_intern_count(t) == 2, "find left the table unchanged");
    CHECK(anostr_is_empty(anostr_sym_str(t, ANOSTR_SYM_NONE)), "sym_str on NONE is empty");
    CHECK(anostr_is_empty(anostr_sym_str(t, 12345)), "sym_str out of range is empty");

    // Dedupe: equal inputs return bit-identical values, so eq never reads the bytes.
    anostr_t d1 = anostr_dedupe(t, anostr_from(heap, lp, strlen(lp)));
    anostr_t d2 = anostr_dedupe(t, anostr_view(lp, strlen(lp)));
    CHECK(memcmp(&d1, &d2, sizeof(anostr_t)) == 0, "deduped values are bit-identical");

    // Growth: enough distinct strings to force slot-table doubling (64 slots, 70% load),
    // then every earlier symbol must still resolve.
    char nameBuf[32];
    anostr_sym syms[300];
    for (int i = 0; i < 300; i++) {
        int n = snprintf(nameBuf, sizeof nameBuf, "entity_type_%03d", i);
        syms[i] = anostr_intern(t, anostr_from(heap, nameBuf, (size_t)n));
        CHECK(syms[i] != ANOSTR_SYM_NONE, "intern under growth");
    }
    CHECK(anostr_intern_count(t) == 302, "300 new + 2 old distinct strings");
    bool stable = true;
    for (int i = 0; i < 300; i++) {
        int n = snprintf(nameBuf, sizeof nameBuf, "entity_type_%03d", i);
        if (anostr_intern_find(t, anostr_from(heap, nameBuf, (size_t)n)) != syms[i])
            stable = false;
    }
    CHECK(stable, "all symbols stable after growth");
    CHECK(anostr_intern_find(t, anostr_lit("hull")) == a, "early symbol survives growth");
}

// Published FNV-1a test vectors (Noll's reference set): the hash itself, at compile time.
static_assert(ANOSTR_SID("") == UINT64_C(0xcbf29ce484222325), "SID: FNV-1a 64 offset basis");
static_assert(ANOSTR_SID("a") == UINT64_C(0xaf63dc4c8601ec8c), "SID: FNV-1a 64 'a'");
static_assert(ANOSTR_SID("foobar") == UINT64_C(0x85944171f73967e8), "SID: FNV-1a 64 'foobar'");
static_assert(ANOSTR_SID32("") == 0x811c9dc5u, "SID32: FNV-1a 32 offset basis");
static_assert(ANOSTR_SID32("a") == 0xe40c292cu, "SID32: FNV-1a 32 'a'");
static_assert(ANOSTR_SID32("foobar") == 0xbf9cf968u, "SID32: FNV-1a 32 'foobar'");

// ICE contexts: enum value, array size, case label, static initializer.
enum {
    SID_ENUM_SPAWN = ANOSTR_SID32("player_spawn"),
    SID_ARRAY_N    = (int)(ANOSTR_SID32("x") & 0xFF) + 1,
};
static char sid_array_size_probe[SID_ARRAY_N];
static const anostr_sid sid_static_table[] = {
    ANOSTR_SID("player_spawn"), ANOSTR_SID("player_death"), ANOSTR_SID("level_loaded"),
};

// A 128-byte literal sits exactly at ANOSTR_SID_MAX; anything longer refuses to compile.
#define SID_16B "0123456789abcdef"
#define SID_128B SID_16B SID_16B SID_16B SID_16B SID_16B SID_16B SID_16B SID_16B

static int sid_dispatch(anostr_sid id)
{
    switch (id) {
    case ANOSTR_SID("player_spawn"):  return 1;
    case ANOSTR_SID("player_death"):  return 2;
    case ANOSTR_SID("level_loaded"):  return 3;
    default:                          return 0;
    }
}

static void test_sid(void)
{
    // The whole point: a compile-time id and a runtime-hashed string meet in one key space.
    CHECK(ANOSTR_SID("player_spawn") == anostr_hash(anostr_lit("player_spawn")),
          "SID: equals anostr_hash of the same literal");
    CHECK(ANOSTR_SID32("player_spawn") == anostr_hash32(anostr_lit("player_spawn")),
          "SID32: equals anostr_hash32 of the same literal");
    CHECK(ANOSTR_SID("a\0b") == anostr_hash(anostr_lit("a\0b")),
          "SID: embedded NUL bytes count, matching anostr_lit");
    CHECK(ANOSTR_SID(SID_128B) == anostr_hash(anostr_lit(SID_128B)),
          "SID: agreement at the 128-byte cap");
    CHECK(ANOSTR_SID("player_spawn") != ANOSTR_SID("player_death"),
          "SID: distinct literals get distinct ids");

    // switch dispatch on compile-time ids, keyed by runtime-computed hashes.
    CHECK(sid_dispatch(anostr_hash(anostr_lit("player_spawn"))) == 1, "SID: case label hit 1");
    CHECK(sid_dispatch(anostr_hash(anostr_lit("player_death"))) == 2, "SID: case label hit 2");
    CHECK(sid_dispatch(anostr_hash(anostr_lit("who?"))) == 0, "SID: default for unknown");

    CHECK(sid_static_table[2] == anostr_hash(anostr_lit("level_loaded")),
          "SID: static initializer holds the right id");
    CHECK((uint32_t)SID_ENUM_SPAWN == anostr_hash32(anostr_lit("player_spawn")),
          "SID32: enum value holds the right id");
    sid_array_size_probe[0] = 1;    // touch it so the probe array is real, not folded away
    CHECK(sizeof sid_array_size_probe == (ANOSTR_SID32("x") & 0xFF) + 1
              && sid_array_size_probe[0] == 1,
          "SID32: usable as an array size");
}

// Randomized round-trip: build a random string via random-sized appends, freeze, and verify
// contents, hash consistency, and a random slice against a plain reference buffer.
static void soak(mi_heap_t *heap, uint32_t iterations)
{
    test_rng rng = rng_make(0x517A1276u);
    char reference[512];
    char chunk[64];

    for (uint32_t it = 0; it < iterations; it++) {
        size_t total = 0;
        anostr_builder_t b = anostr_builder_make(heap, rng_below(&rng, 32));
        uint32_t appends = 1 + rng_below(&rng, 8);
        for (uint32_t a = 0; a < appends && total < 448; a++) {
            size_t n = rng_fill_printable(&rng, chunk, 0, 63);
            memcpy(reference + total, chunk, n);
            total += n;
            if (anostr_builder_append(&b, chunk, n) != 0) {
                printf("FAIL: soak append (it=%u)\n", it);
                failures++;
            }
        }
        anostr_t s = anostr_freeze(&b);
        if (!str_equals_mem(s, reference, total)) {
            printf("FAIL: soak round-trip (it=%u, len=%zu)\n", it, total);
            failures++;
        }
        if (anostr_hash(s) != anostr_hash(anostr_from(heap, reference, total))) {
            printf("FAIL: soak hash mismatch (it=%u)\n", it);
            failures++;
        }
        size_t start = rng_below(&rng, (uint32_t)total + 1);
        size_t end   = start + rng_below(&rng, (uint32_t)(total - start) + 1);
        if (!str_equals_mem(anostr_slice(s, start, end), reference + start, end - start)) {
            printf("FAIL: soak slice (it=%u, [%zu,%zu))\n", it, start, end);
            failures++;
        }
    }
}

int main(int argc, char **argv)
{
    // One scratch heap for the whole run; everything long-lived dies with it at exit.
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }

    test_construction_roundtrip(heap);
    test_padding_makes_equals_bitwise(heap);
    test_eq_compare_hash(heap);
    test_slice(heap);
    test_keep(heap);
    test_builder(heap);
    test_find_concat_join(heap);
    test_split(heap);
    test_intern(heap);
    test_sid();

    uint32_t iterations = 2000;
    if (argc > 1) iterations = (uint32_t)strtoul(argv[1], NULL, 10);
    soak(heap, iterations);

    if (failures == 0) { printf("anotest_strings: all checks passed\n"); return 0; }
    printf("anotest_strings: %d check(s) failed\n", failures);
    return 1;
}
