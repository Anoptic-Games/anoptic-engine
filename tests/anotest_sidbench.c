/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Benchmark for ANOSTR_SID: what build-time hashing deletes from string-keyed dispatch.
 *
 * Scenario: an event system with 16 known event types dispatching a random event stream.
 * Each event carries its OWN copy of the name bytes (as a parsed message would), so no
 * series gets a pointer-identity shortcut; the sid series never touches bytes at all.
 * Series, per-event latency (batch of 64 per timed sample):
 *   - strcmp chain:        event arrives as a C string, linear strcmp over the 16 names;
 *   - anostr_eq chain:     event arrives as anostr_t, linear anostr_eq (prefix fast path);
 *   - hash64 + switch:     event as anostr_t, anostr_hash then switch on ANOSTR_SID labels
 *                          (the runtime-hashing sibling: FNV paid per event);
 *   - intern_find + sym:   event as anostr_t, anostr_intern_find then dense-sym jump
 *                          (hash + table probe per event);
 *   - sid switch (baked):  the key is already an ANOSTR_SID constant, switch on a u64 --
 *                          no hashing, no string bytes, the compile-time payoff.
 * Oracle: every series dispatches the same stream, so all handler-index sums must agree
 * (mismatch prints and exits 1).
 *
 * Bulk keying: the startup cost SID deletes. 20k distinct identifiers pushed through
 * anostr_intern (insert, then re-key warm), reported as ns/key and total ms; the SID column
 * of that table is zero by construction (ids are baked into .rodata at build).
 *
 * Prints bench.h percentile tables. Built always, DISABLED in ctest; run by hand from a
 * Release (-O3) build (build 7). argv[1] scales the event count. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_strings.h"
#include "templates/bench.h"
#include "templates/rng.h"

#define EVENTS_DEFAULT 200000u
#define BATCH 64u
#define NTYPES 16u
#define BULK_KEYS 20000u

// A volatile sink so dispatch results cannot be optimized away.
static volatile uint64_t g_sink;

// The 16 known event types: mixed lengths, several past the 12-byte inline cap.
#define EVENT_LIST(X) \
    X(0,  "tick")             X(1,  "player_spawn")    \
    X(2,  "player_death")     X(3,  "level_loaded")    \
    X(4,  "level_unloaded")   X(5,  "asset_streamed")  \
    X(6,  "collision_enter")  X(7,  "collision_exit")  \
    X(8,  "input_keydown")    X(9,  "input_keyup")     \
    X(10, "net_packet_in")    X(11, "net_packet_out")  \
    X(12, "audio_cue")        X(13, "ui_open")         \
    X(14, "ui_close")         X(15, "shader_reload")

#define AS_CSTR(i, s) s,
static const char *const g_names[NTYPES] = { EVENT_LIST(AS_CSTR) };

#define AS_SID(i, s) ANOSTR_SID(s),
static const anostr_sid g_sids[NTYPES] = { EVENT_LIST(AS_SID) };

// The consumer side of the SID design: a switch over build-time constants.
static inline uint32_t dispatch_sid(anostr_sid id)
{
    switch (id) {
#define AS_CASE(i, s) case ANOSTR_SID(s): return i;
    EVENT_LIST(AS_CASE)
#undef AS_CASE
    default: return UINT32_MAX;
    }
}

static inline uint32_t dispatch_strcmp(const char *name)
{
    for (uint32_t i = 0; i < NTYPES; i++)
        if (strcmp(name, g_names[i]) == 0)
            return i;
    return UINT32_MAX;
}

static inline uint32_t dispatch_eq(anostr_t s, const anostr_t *names)
{
    for (uint32_t i = 0; i < NTYPES; i++)
        if (anostr_eq(s, names[i]))
            return i;
    return UINT32_MAX;
}

static inline uint32_t dispatch_intern(const anostr_intern_t *t, anostr_t s)
{
    anostr_sym sym = anostr_intern_find(t, s);
    return sym == ANOSTR_SYM_NONE ? UINT32_MAX : sym;
}

// One timed series: body(i) dispatches event i and returns the handler index.
#define RUN_SERIES(label, sumVar, body)                                     \
    do {                                                                    \
        bench_lat lat;                                                      \
        bench_lat_init(&lat, buf, cap);                                     \
        uint64_t sum = 0;                                                   \
        for (uint32_t i = 0; i + BATCH <= events; i += BATCH) {             \
            uint64_t t0 = bench_begin();                                    \
            uint64_t acc = 0;                                               \
            for (uint32_t j = 0; j < BATCH; j++) {                          \
                uint32_t idx = i + j;                                       \
                acc += (body);                                              \
            }                                                               \
            g_sink = acc;                                                   \
            bench_lat_add(&lat, bench_end(t0) / BATCH);                     \
            sum += acc;                                                     \
        }                                                                   \
        (sumVar) = sum;                                                     \
        bench_lat_row((label), bench_lat_stats(&lat));                      \
    } while (0)

/* Bulk reads: read latency of resolving a random query stream against 50k distinct records
 * under four lookup strategies. Same conventions as above: one LOCALHEAPATTR heap, all fixtures
 * and maps mi_heap_malloc'd from it, a volatile g_sink, batched bench_lat samples, an oracle
 * that exits 1 on disagreement. Built always, DISABLED in ctest; run from a Release (-O3) build.
 * argv[2] scales the lookup count.
 *   1 intern     -- anostr_intern_find: hash + table probe + anostr_eq confirm (touches bytes);
 *   2 sid map    -- open-addressed u64->u32: integer-only reads, no byte deref (ANOSTR_SID ceiling);
 *   3 hash+eq    -- open-addressed by anostr_hash: hash + anostr_eq confirm (touches bytes);
 *   4 bsearch    -- sorted-sid array: O(log n) integer compares, no hashing, no byte deref.
 * Strategies 2 and 4 read the precomputed q.sid; 1 and 3 read q.str. */

#define RECORDS 50000u
#define LOOKUPS_DEFAULT 2000000u
#define MAP_CAP 131072u          // next_pow2(RECORDS / 0.5), load <= 0.5
#define MAP_MASK (MAP_CAP - 1u)

typedef struct { const char *name; anostr_t str; anostr_sid sid; uint32_t index; } record;

// sid -> index, open-addressed, linear probe; idx == UINT32_MAX marks empty.
typedef struct { uint64_t sid; uint32_t idx; } sidslot;
// {str, hash} -> index, open-addressed, linear probe; idx == UINT32_MAX marks empty.
typedef struct { anostr_t str; uint64_t hash; uint32_t idx; } hashslot;
// sorted-by-sid array element for bsearch.
typedef struct { uint64_t sid; uint32_t idx; } sidrec;

// Bit-mix for a well-distributed slot from an already-hashed 64-bit sid.
static inline uint64_t splitmix64(uint64_t x)
{
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

// Strategy 2: integer-only probe, no byte deref -- the baked-ANOSTR_SID ceiling.
static inline uint32_t sidmap_find(const sidslot *m, uint64_t sid)
{
    uint32_t slot = (uint32_t)(splitmix64(sid) & MAP_MASK);
    for (;;) {
        if (m[slot].idx == UINT32_MAX) return UINT32_MAX;
        if (m[slot].sid == sid)        return m[slot].idx;
        slot = (slot + 1u) & MAP_MASK;
    }
}

// Strategy 3: probe by hash, confirm with anostr_eq -- hash is not identity, reads the bytes.
static inline uint32_t hashmap_find(const hashslot *m, anostr_t q, uint64_t h)
{
    uint32_t slot = (uint32_t)(h & MAP_MASK);
    for (;;) {
        if (m[slot].idx == UINT32_MAX)                  return UINT32_MAX;
        if (m[slot].hash == h && anostr_eq(m[slot].str, q)) return m[slot].idx;
        slot = (slot + 1u) & MAP_MASK;
    }
}

static int cmp_sidrec(const void *a, const void *b)
{
    uint64_t x = ((const sidrec *)a)->sid, y = ((const sidrec *)b)->sid;
    return x < y ? -1 : x > y;
}

// Strategy 4: bsearch the sorted sid array -- log n integer compares, no hashing.
static inline uint32_t bsearch_find(const sidrec *arr, size_t n, uint64_t sid)
{
    sidrec key = { sid, 0 };
    const sidrec *r = bsearch(&key, arr, n, sizeof *arr, cmp_sidrec);
    return r ? r->idx : UINT32_MAX;
}

// One timed read series over the shared stream; batches of BATCH, per-lookup ns into lat,
// summed batch ticks into tpVar for throughput, resolved indices summed into sumVar for the oracle.
#define RUN_READS(label, sumVar, tpVar, body)                               \
    do {                                                                    \
        bench_lat lat;                                                      \
        bench_lat_init(&lat, rbuf, rcap);                                   \
        uint64_t sum = 0, totalTicks = 0;                                   \
        for (uint32_t i = 0; i + BATCH <= lookups; i += BATCH) {            \
            uint64_t t0 = bench_begin();                                    \
            uint64_t acc = 0;                                               \
            for (uint32_t j = 0; j < BATCH; j++) {                          \
                uint32_t k = stream[i + j];                                 \
                acc += (body);                                             \
            }                                                               \
            g_sink = acc;                                                   \
            uint64_t d = bench_end(t0);                                     \
            totalTicks += d;                                                \
            bench_lat_add(&lat, d / BATCH);                                 \
            sum += acc;                                                     \
        }                                                                   \
        (sumVar) = sum;                                                     \
        (tpVar)  = ano_ticks_to_ns(totalTicks);                            \
        bench_lat_row((label), bench_lat_stats(&lat));                      \
    } while (0)

// The whole bulk-reads run over the shared heap. Returns 0, or 1 on oracle/build failure.
static int bulkreads_run(mi_heap_t *heap, uint32_t lookups)
{
    // Fixtures: 50k distinct records, names past the 12-byte inline cap (the long-value case).
    record *records = mi_heap_malloc(heap, RECORDS * sizeof *records);
    sidslot *sidmap = mi_heap_malloc(heap, MAP_CAP * sizeof *sidmap);
    hashslot *hmap  = mi_heap_malloc(heap, MAP_CAP * sizeof *hmap);
    sidrec  *sorted = mi_heap_malloc(heap, RECORDS * sizeof *sorted);
    if (!records || !sidmap || !hmap || !sorted) { printf("alloc failed\n"); return 1; }
    for (uint32_t s = 0; s < MAP_CAP; s++) { sidmap[s].idx = UINT32_MAX; hmap[s].idx = UINT32_MAX; }

    test_rng rng = rng_make(0xB0BABEEFu);
    char nameBuf[64], suffix[8];
    for (uint32_t i = 0; i < RECORDS; i++) {
        size_t sn = rng_fill_printable(&rng, suffix, 3, 7);
        (void)sn;
        int n = snprintf(nameBuf, sizeof nameBuf, "assets/props/entity_%05u_%s.gltf", i, suffix);
        char *nm = mi_heap_malloc(heap, (size_t)n + 1);
        if (nm == NULL) { printf("alloc failed\n"); return 1; }
        memcpy(nm, nameBuf, (size_t)n + 1);
        records[i].name  = nm;
        records[i].str   = anostr_from(heap, nameBuf, (size_t)n);  // record owns its arena bytes
        // Runtime anostr_hash stands in for the compile-time ANOSTR_SID a real call site bakes;
        // 50k literals are impossible, so the stream's sid is precomputed once, off the timed path:
        // at a real call site strategies 2 and 4 pay ZERO hashing (the key is already a constant).
        records[i].sid   = anostr_hash(records[i].str);
        records[i].index = i;
    }

    // Strategy 1: one intern table, records inserted in order so sym i == record i.
    anostr_intern_t *itab = anostr_intern_make(heap);
    if (itab == NULL) { printf("intern table failed\n"); return 1; }
    for (uint32_t i = 0; i < RECORDS; i++)
        if (anostr_intern(itab, records[i].str) != i) { printf("intern order broke\n"); return 1; }

    // Strategy 2: sid -> index map. Duplicate sid means the two sid-keyed strategies would alias.
    for (uint32_t i = 0; i < RECORDS; i++) {
        uint64_t sid = records[i].sid;
        uint32_t slot = (uint32_t)(splitmix64(sid) & MAP_MASK);
        while (sidmap[slot].idx != UINT32_MAX) {
            if (sidmap[slot].sid == sid) {
                printf("ORACLE FAILED: duplicate sid %llu (records %u and %u)\n",
                       (unsigned long long)sid, sidmap[slot].idx, i);
                return 1;
            }
            slot = (slot + 1u) & MAP_MASK;
        }
        sidmap[slot].sid = sid;
        sidmap[slot].idx = i;
    }

    // Strategy 3: {str, hash} -> index map, slot from anostr_hash.
    for (uint32_t i = 0; i < RECORDS; i++) {
        uint64_t h = records[i].sid;                 // sid == anostr_hash(str), reuse it
        uint32_t slot = (uint32_t)(h & MAP_MASK);
        while (hmap[slot].idx != UINT32_MAX)
            slot = (slot + 1u) & MAP_MASK;
        hmap[slot].str  = records[i].str;
        hmap[slot].hash = h;
        hmap[slot].idx  = i;
    }

    // Strategy 4: sorted-by-sid array for bsearch.
    for (uint32_t i = 0; i < RECORDS; i++) { sorted[i].sid = records[i].sid; sorted[i].idx = i; }
    qsort(sorted, RECORDS, sizeof *sorted, cmp_sidrec);

    // Query stream: record indices, drawn once so RNG cost stays out of the timed section.
    uint32_t *stream = mi_heap_malloc(heap, lookups * sizeof *stream);
    size_t    rcap   = lookups / BATCH + 1;
    uint64_t *rbuf   = mi_heap_malloc(heap, rcap * sizeof *rbuf);
    if (!stream || !rbuf) { printf("alloc failed\n"); return 1; }
    for (uint32_t i = 0; i < lookups; i++)
        stream[i] = rng_below(&rng, RECORDS);

    // Reference sum: the drawn indices over the timed range; every strategy must match it.
    uint32_t timed = (lookups / BATCH) * BATCH;
    uint64_t refSum = 0;
    for (uint32_t i = 0; i < timed; i++) refSum += stream[i];

    // One untimed warm pass: fault pages, prime caches, and fully verify every resolution against
    // the drawn index -- an order-sensitive, full-stream oracle (a plain sum can hide a swap).
    uint64_t warm = 0;
    for (uint32_t i = 0; i < timed; i++) {
        uint32_t k = stream[i];
        uint32_t r1 = (uint32_t)anostr_intern_find(itab, records[k].str);
        uint32_t r2 = sidmap_find(sidmap, records[k].sid);
        uint32_t r3 = hashmap_find(hmap, records[k].str, records[k].sid);
        uint32_t r4 = bsearch_find(sorted, RECORDS, records[k].sid);
        if (r1 != k || r2 != k || r3 != k || r4 != k) {
            printf("ORACLE FAILED: misresolved lookup %u (want %u: %u %u %u %u)\n", i, k, r1, r2, r3, r4);
            return 1;
        }
        warm += (uint64_t)r1 + r2 + r3 + r4;
    }
    g_sink = warm;

    printf("\nanotest_bulkreads: %u records, %u lookups, %u reads per sample\n\n",
           RECORDS, timed, BATCH);
    bench_lat_header();

    uint64_t sumIntern, sumSidmap, sumHashmap, sumBsearch;
    uint64_t tpIntern, tpSidmap, tpHashmap, tpBsearch;
    RUN_READS("intern_find + sym",  sumIntern,  tpIntern,
              anostr_intern_find(itab, records[k].str));
    RUN_READS("sid map (int only)", sumSidmap,  tpSidmap,
              sidmap_find(sidmap, records[k].sid));
    RUN_READS("hash + eq confirm",  sumHashmap, tpHashmap,
              hashmap_find(hmap, records[k].str, anostr_hash(records[k].str)));
    RUN_READS("sorted-sid bsearch", sumBsearch, tpBsearch,
              bsearch_find(sorted, RECORDS, records[k].sid));

    if (sumIntern != refSum || sumSidmap != refSum ||
        sumHashmap != refSum || sumBsearch != refSum) {
        printf("\nORACLE FAILED: resolved sums disagree (ref %llu: %llu %llu %llu %llu)\n",
               (unsigned long long)refSum,   (unsigned long long)sumIntern,
               (unsigned long long)sumSidmap,(unsigned long long)sumHashmap,
               (unsigned long long)sumBsearch);
        return 1;
    }

    printf("\nthroughput (lookups/s):\n");
    printf("  intern_find + sym:   %12.0f  (hash + probe + anostr_eq, touches bytes)\n",
           bench_ops_per_sec(timed, tpIntern));
    printf("  sid map (int only):  %12.0f  (integer-only probe, what a baked ANOSTR_SID buys)\n",
           bench_ops_per_sec(timed, tpSidmap));
    printf("  hash + eq confirm:   %12.0f  (hash + anostr_eq, touches bytes)\n",
           bench_ops_per_sec(timed, tpHashmap));
    printf("  sorted-sid bsearch:  %12.0f  (log n integer compares, no hashing)\n",
           bench_ops_per_sec(timed, tpBsearch));
    printf("note: strategy 2 is integer-only reads (the ANOSTR_SID payoff); 1 and 3 hash and touch\n"
           "      string bytes; 4 pays log n integer compares.\n");
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t events = EVENTS_DEFAULT;
    if (argc > 1) events = (uint32_t)strtoul(argv[1], NULL, 10);

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("mi_heap_new failed\n"); return 1; }

    // Shared fixtures: anostr_t names, a pre-populated intern table (sym i == type i).
    anostr_t names[NTYPES];
    for (uint32_t i = 0; i < NTYPES; i++)
        names[i] = anostr_from_cstr(heap, g_names[i]);
    anostr_intern_t *table = anostr_intern_make(heap);
    if (table == NULL) { printf("intern table failed\n"); return 1; }
    for (uint32_t i = 0; i < NTYPES; i++)
        if (anostr_intern(table, names[i]) != i) { printf("intern order broke\n"); return 1; }

    // One event stream, all representations of it derived from the same indices.
    test_rng rng = rng_make(0x51D51D51u);
    const char **asCstr = mi_heap_malloc(heap, events * sizeof *asCstr);
    anostr_t   *asStr  = mi_heap_malloc(heap, events * sizeof *asStr);
    anostr_sid *asSid  = mi_heap_malloc(heap, events * sizeof *asSid);
    uint64_t   *buf    = mi_heap_malloc(heap, (events / BATCH + 1) * sizeof *buf);
    if (!asCstr || !asStr || !asSid || !buf) { printf("alloc failed\n"); return 1; }
    for (uint32_t i = 0; i < events; i++) {
        uint32_t t = rng_below(&rng, NTYPES);
        size_t   n = strlen(g_names[t]);
        char *copy = mi_heap_malloc(heap, n + 1);
        if (copy == NULL) { printf("alloc failed\n"); return 1; }
        memcpy(copy, g_names[t], n + 1);
        asCstr[i] = copy;
        asStr[i]  = anostr_view(copy, n);   // long names borrow the event's own bytes
        asSid[i]  = g_sids[t];  // at a real call site this is ANOSTR_SID("..."), baked
    }

    size_t cap = events / BATCH + 1;
    printf("anotest_sidbench: %u events over %u types, %u dispatches per sample\n\n",
           events, NTYPES, BATCH);
    bench_lat_header();

    uint64_t sumStrcmp, sumEq, sumHash, sumIntern, sumSid;
    RUN_SERIES("strcmp chain",        sumStrcmp, dispatch_strcmp(asCstr[idx]));
    RUN_SERIES("anostr_eq chain",     sumEq,     dispatch_eq(asStr[idx], names));
    RUN_SERIES("hash64 + sid switch", sumHash,   dispatch_sid(anostr_hash(asStr[idx])));
    RUN_SERIES("intern_find + sym",   sumIntern, dispatch_intern(table, asStr[idx]));
    RUN_SERIES("sid switch (baked)",  sumSid,    dispatch_sid(asSid[idx]));

    if (sumStrcmp != sumEq || sumEq != sumHash || sumHash != sumIntern || sumIntern != sumSid) {
        printf("\nORACLE FAILED: dispatch sums disagree (%llu %llu %llu %llu %llu)\n",
               (unsigned long long)sumStrcmp, (unsigned long long)sumEq,
               (unsigned long long)sumHash,   (unsigned long long)sumIntern,
               (unsigned long long)sumSid);
        return 1;
    }

    // Bulk keying: what SID deletes at startup. 20k distinct identifiers, insert then re-key.
    char nameBuf[48];
    anostr_t *bulk = mi_heap_malloc(heap, BULK_KEYS * sizeof *bulk);
    if (bulk == NULL) { printf("alloc failed\n"); return 1; }
    for (uint32_t i = 0; i < BULK_KEYS; i++) {
        int n = snprintf(nameBuf, sizeof nameBuf, "assets/props/entity_%05u.gltf", i);
        bulk[i] = anostr_from(heap, nameBuf, (size_t)n);
    }
    anostr_intern_t *bulkTable = anostr_intern_make(heap);
    if (bulkTable == NULL) { printf("intern table failed\n"); return 1; }

    uint64_t t0 = bench_begin();
    for (uint32_t i = 0; i < BULK_KEYS; i++)
        if (anostr_intern(bulkTable, bulk[i]) == ANOSTR_SYM_NONE) { printf("intern failed\n"); return 1; }
    uint64_t insertNs = ano_ticks_to_ns(bench_end(t0));

    t0 = bench_begin();
    uint64_t acc = 0;
    for (uint32_t i = 0; i < BULK_KEYS; i++)
        acc += anostr_intern_find(bulkTable, bulk[i]);
    g_sink = acc;
    uint64_t rekeyNs = ano_ticks_to_ns(bench_end(t0));

    printf("\nbulk keying, %u distinct identifiers:\n", BULK_KEYS);
    printf("  runtime intern (insert):  %8.2f ms total, %6.1f ns/key, %10.0f keys/s\n",
           (double)insertNs / 1e6, (double)insertNs / BULK_KEYS,
           bench_ops_per_sec(BULK_KEYS, insertNs));
    printf("  runtime re-key (find):    %8.2f ms total, %6.1f ns/key, %10.0f keys/s\n",
           (double)rekeyNs / 1e6, (double)rekeyNs / BULK_KEYS,
           bench_ops_per_sec(BULK_KEYS, rekeyNs));
    printf("  comptime ANOSTR_SID:          0.00 ms total,    0.0 ns/key (baked at build)\n");

    // Bulk reads: 50k records resolved four ways. argv[2] scales the lookup count.
    uint32_t lookups = LOOKUPS_DEFAULT;
    if (argc > 2) lookups = (uint32_t)strtoul(argv[2], NULL, 10);
    if (bulkreads_run(heap, lookups) != 0) return 1;
    return 0;
}
