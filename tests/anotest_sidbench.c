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
    return 0;
}
