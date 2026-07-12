/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The registry: rid -> slot map with generations, single-copy enforcement, owned
// payloads, and gamesave loading. The intern table's shape generalized (sparse pow2
// slot array storing slot+1, dense rows, grow-before-mutate) with two deliberate
// divergences: a rid binds to its row FOREVER (no tombstones, no slot recycling --
// collision-proof and debuggable; a game's distinct-path population bounds the table),
// and liveness rides a per-row generation that bumps on every load/unload/release, so
// every outstanding copy of a stale handle goes politely invalid.
//
// Placement (the section-5 bake-off's model A, the null hypothesis): one shared
// multipool, mutex-guarded ("rings for queues, mutex for maps"), over the DEFAULT-heap
// parent -- mi_heap_t parents are single-thread-owner and the registry is cross-thread
// by contract. Payloads past the multipool's top class are standalone mi allocations:
// zero-copy ano_res_release hands the block to the taker, who frees it with
// ano_aligned_free. Pool-resident payloads copy out on release and their block recycles.
//
// Threading: one mutex guards map + rows + placement. Sync loads run under it --
// single-copy comes free, and the async tier (plan step 5) moves loads off-thread
// without changing this contract.

#include <anoptic_resources.h>

#include <anoptic_log.h>
#include <anoptic_threads.h>

#include <stdio.h>
#include <string.h>

#include "resources_internal.h"

#define REG_INITIAL_SLOTS 64u                   // pow2
#define REG_ROW_INITIAL   32u

typedef struct res_row {
    uint64_t rid;
    char    *name;          // logical path, NUL-terminated, mi_malloc'd (diagnostics +
    uint32_t name_len;      // rid-collision detection)
    uint32_t gen;           // bumps on load AND unload: loaded gens are odd, 0 = virgin
    void    *data;          // size + 1 bytes (guard NUL); NULL when not loaded
    size_t   size;
    bool     direct;        // standalone mi allocation (zero-copy release) vs pooled
} res_row;

static struct {
    anothread_mutex_t mtx;
    bool      alive;
    uint32_t *slots;        // row index + 1; 0 empty. pow2, linear probe on rid.
    uint32_t  slot_mask;
    res_row  *rows;
    uint32_t  row_count;
    uint32_t  row_cap;
    ano_mem_multipool *pool;    // shared payload multipool (model A)
    size_t    pool_max;         // its top class; bigger payloads go direct
} g_reg;

int res_registry_init(void)
{
    if (ano_mutex_init(&g_reg.mtx, NULL) != 0)
        return -1;
    g_reg.slots = mi_zalloc(REG_INITIAL_SLOTS * sizeof *g_reg.slots);
    g_reg.rows  = mi_malloc(REG_ROW_INITIAL * sizeof *g_reg.rows);
    g_reg.pool  = ano_mem_multipool_make(ano_mem_parent_default(), NULL);
    if (g_reg.slots == NULL || g_reg.rows == NULL || g_reg.pool == NULL) {
        mi_free(g_reg.slots);
        mi_free(g_reg.rows);
        ano_mem_multipool_destroy(g_reg.pool);
        ano_mutex_destroy(&g_reg.mtx);
        return -1;
    }
    g_reg.slot_mask = REG_INITIAL_SLOTS - 1;
    g_reg.row_cap   = REG_ROW_INITIAL;
    g_reg.row_count = 0;
    g_reg.pool_max  = 1u << 20;                 // the default multipool max_block
    g_reg.alive     = true;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Map internals (the intern-table walk, keyed by rid -- the rid IS the cached hash).

static uint32_t probe_find(uint64_t rid)
{
    uint32_t idx = (uint32_t)rid & g_reg.slot_mask;
    while (g_reg.slots[idx] != 0) {
        uint32_t row = g_reg.slots[idx] - 1;
        if (g_reg.rows[row].rid == rid)
            return row;
        idx = (idx + 1) & g_reg.slot_mask;
    }
    return UINT32_MAX;
}

static void slot_insert(uint64_t rid, uint32_t row)
{
    uint32_t idx = (uint32_t)rid & g_reg.slot_mask;
    while (g_reg.slots[idx] != 0)
        idx = (idx + 1) & g_reg.slot_mask;
    g_reg.slots[idx] = row + 1;
}

static int grow_slots(void)
{
    uint32_t old_cap = g_reg.slot_mask + 1;
    if (old_cap > UINT32_MAX / 2)
        return -1;
    uint32_t *fresh = mi_zalloc((size_t)old_cap * 2 * sizeof *fresh);
    if (fresh == NULL)
        return -1;
    uint32_t *old = g_reg.slots;
    g_reg.slots     = fresh;
    g_reg.slot_mask = old_cap * 2 - 1;
    for (uint32_t r = 0; r < g_reg.row_count; r++)
        slot_insert(g_reg.rows[r].rid, r);
    mi_free(old);
    return 0;
}

// Find-or-create the permanent row for (rid, logical). UINT32_MAX on OOM or collision.
static uint32_t row_bind(uint64_t rid, const char *logical, size_t len)
{
    uint32_t row = probe_find(rid);
    if (row != UINT32_MAX) {
        if (g_reg.rows[row].name_len != (uint32_t)len
            || memcmp(g_reg.rows[row].name, logical, len) != 0) {
            // A 64-bit FNV collision between two live logical paths. Loud, refused.
            ano_log(ANO_ERROR, "resources: rid collision: '%s' vs '%s'",
                    g_reg.rows[row].name, logical);
            return UINT32_MAX;
        }
        return row;
    }
    // Grow-before-mutate, the intern discipline.
    if (g_reg.row_count >= UINT32_MAX - 1)
        return UINT32_MAX;
    if ((uint64_t)(g_reg.row_count + 1) * 10 > (uint64_t)(g_reg.slot_mask + 1) * 7)
        if (grow_slots() != 0)
            return UINT32_MAX;
    if (g_reg.row_count == g_reg.row_cap) {
        if (g_reg.row_cap > UINT32_MAX / 2)
            return UINT32_MAX;
        res_row *fresh = mi_realloc(g_reg.rows, (size_t)g_reg.row_cap * 2 * sizeof *fresh);
        if (fresh == NULL)
            return UINT32_MAX;
        g_reg.rows    = fresh;
        g_reg.row_cap = g_reg.row_cap * 2;
    }
    char *name = mi_malloc(len + 1);
    if (name == NULL)
        return UINT32_MAX;
    memcpy(name, logical, len);
    name[len] = '\0';
    row = g_reg.row_count++;
    g_reg.rows[row] = (res_row){ .rid = rid, .name = name, .name_len = (uint32_t)len };
    slot_insert(rid, row);
    return row;
}

// Valid, loaded row for a handle, or NULL. Callers hold the mutex.
static res_row *row_of(anores_t h)
{
    if (!g_reg.alive || h.slot >= g_reg.row_count)
        return NULL;
    res_row *row = &g_reg.rows[h.slot];
    if (row->rid != h.rid || row->gen != h.gen || row->data == NULL)
        return NULL;
    return row;
}

// ---------------------------------------------------------------------------------------------
// Placement: pooled vs direct, per the top-class threshold. alloc includes the guard NUL.

static void *payload_alloc(size_t size, bool *direct)
{
    if (size + 1 > g_reg.pool_max) {
        *direct = true;
        return mi_malloc_aligned(size + 1, ANO_CACHE_LINE);
    }
    *direct = false;
    return ano_mem_multipool_alloc(g_reg.pool, size + 1);
}

static void payload_free(res_row *row)
{
    if (row->direct)
        mi_free(row->data);
    else
        ano_mem_multipool_free(g_reg.pool, row->data, row->size + 1);
    row->data = NULL;
    row->size = 0;
}

// Install a loaded payload (takes ownership of buf, a default-heap mi allocation from
// res_read_all). Direct-class payloads keep buf itself: read-to-home, zero copy.
static int row_install(res_row *row, void *buf, size_t size)
{
    if (size + 1 > g_reg.pool_max) {
        row->data   = buf;
        row->direct = true;
    } else {
        bool direct;
        void *home = payload_alloc(size, &direct);
        if (home == NULL) {
            mi_free(buf);
            return -1;
        }
        memcpy(home, buf, size + 1);            // the guard NUL rides along
        mi_free(buf);
        row->data   = home;
        row->direct = direct;
    }
    row->size = size;
    row->gen += 1;                              // this load's generation
    return 0;
}

// ---------------------------------------------------------------------------------------------
// The public handle tier.

anores_t ano_res_get(const char *logical)
{
    anores_t none = {0};
    size_t len;
    if (res_path_validate(logical, &len) != 0) {
        ano_log(ANO_ERROR, "resources: get refused invalid path");
        return none;
    }
    if (!res_ready()) {
        ano_log(ANO_ERROR, "resources: get before init: %s", logical);
        return none;
    }
    res_freeze();
    uint64_t rid = res_fnv1a64(logical, len);   // == anostr_hash == ANOSTR_SID

    ano_mutex_lock(&g_reg.mtx);
    uint32_t slot = row_bind(rid, logical, len);
    if (slot == UINT32_MAX) {
        ano_mutex_unlock(&g_reg.mtx);
        ano_log(ANO_ERROR, "resources: get could not bind a row: %s", logical);
        return none;
    }
    res_row *row = &g_reg.rows[slot];
    if (row->data != NULL) {                    // single-copy: same path = same handle
        anores_t h = { rid, slot, row->gen };
        ano_mutex_unlock(&g_reg.mtx);
        return h;
    }
    // Load under the lock: sync load is the primitive, single-copy comes free.
    ano_fspath cand[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, cand, ANO_RES_MAX_MOUNTS + 2);
    for (int i = 0; i < n; i++) {
        void  *buf  = NULL;
        size_t size = 0;
        int rc = res_read_all(NULL, cand[i].str, &buf, &size);
        if (rc == -1)
            continue;                           // this root cannot open it: shadow on
        if (rc != 0) {
            ano_mutex_unlock(&g_reg.mtx);
            ano_log(ANO_ERROR, "resources: get read failed mid-file: %s", cand[i].str);
            return none;
        }
        if (row_install(row, buf, size) != 0) {
            ano_mutex_unlock(&g_reg.mtx);
            ano_log(ANO_ERROR, "resources: get could not place %zu bytes: %s",
                    size, logical);
            return none;
        }
        anores_t h = { rid, slot, row->gen };
        ano_mutex_unlock(&g_reg.mtx);
        return h;
    }
    ano_mutex_unlock(&g_reg.mtx);
    ano_log(ANO_ERROR, "resources: not found in any mount: %s", logical);
    return none;
}

anostr_t ano_res_bytes(anores_t h)
{
    if (!res_ready())
        return anostr_empty();
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_of(h);
    if (row == NULL || row->size > UINT32_MAX) {
        ano_mutex_unlock(&g_reg.mtx);
        return anostr_empty();
    }
    // The view borrows the payload; it stays valid after unlock until the generation
    // retires -- that is the handle contract, not the mutex's.
    anostr_t v = anostr_view(row->data, row->size);
    ano_mutex_unlock(&g_reg.mtx);
    return v;
}

int ano_res_release(anores_t h, void **data, size_t *size)
{
    if (data == NULL || size == NULL || !res_ready())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_of(h);
    if (row == NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    if (row->direct) {
        *data = row->data;                      // zero-copy hand-off
        *size = row->size;
        row->data = NULL;
        row->size = 0;
    } else {
        void *out = mi_malloc_aligned(row->size + 1, ANO_CACHE_LINE);
        if (out == NULL) {
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
        memcpy(out, row->data, row->size + 1);
        *data = out;
        *size = row->size;
        payload_free(row);
    }
    row->gen += 1;                              // outstanding views die here
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

int ano_res_unload(anores_t h)
{
    if (!res_ready())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_of(h);
    if (row == NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    payload_free(row);
    row->gen += 1;
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Extension access.

int res_registry_name(anores_t h, char *out, size_t cap)
{
    if (out == NULL || cap == 0 || !res_ready())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_of(h);
    if (row == NULL || (size_t)row->name_len + 1 > cap) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    memcpy(out, row->name, (size_t)row->name_len + 1);
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

anores_t res_registry_find(const char *logical)
{
    anores_t none = {0};
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready())
        return none;
    uint64_t rid = res_fnv1a64(logical, len);
    ano_mutex_lock(&g_reg.mtx);
    uint32_t row = probe_find(rid);
    anores_t h = none;
    if (row != UINT32_MAX && g_reg.rows[row].data != NULL)
        h = (anores_t){ rid, row, g_reg.rows[row].gen };
    ano_mutex_unlock(&g_reg.mtx);
    return h;
}

anores_t res_registry_adopt(const char *logical, void *buf, size_t size)
{
    anores_t none = {0};
    if (buf == NULL)
        return none;
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready()) {
        mi_free(buf);
        return none;
    }
    uint64_t rid = res_fnv1a64(logical, len);
    ano_mutex_lock(&g_reg.mtx);
    uint32_t slot = row_bind(rid, logical, len);
    if (slot == UINT32_MAX) {
        ano_mutex_unlock(&g_reg.mtx);
        mi_free(buf);
        return none;
    }
    res_row *row = &g_reg.rows[slot];
    if (row->data != NULL) {                    // single-copy: the first adopter won
        anores_t h = { rid, slot, row->gen };
        ano_mutex_unlock(&g_reg.mtx);
        mi_free(buf);
        return h;
    }
    if (row_install(row, buf, size) != 0) {     // frees buf on failure
        ano_mutex_unlock(&g_reg.mtx);
        return none;
    }
    anores_t h = { rid, slot, row->gen };
    ano_mutex_unlock(&g_reg.mtx);
    return h;
}

// ---------------------------------------------------------------------------------------------
// Gamesave loading: newest-seq-first, fresh handle each, framing + hashes only, the
// frame's seq must echo the filename's (rename-masquerade refused). Orphan temps tried
// last, then purged. First valid wins and becomes the owned resource "saves/<slot>".

typedef struct load_scan {
    const char *slot;
    size_t      slot_len;
    uint64_t    seqs[64];                       // NEWEST 64 seen, descending
    int         count;
    char        tmps[8][MAXPATH];               // orphaned protocol temps, tried last
    int         tmp_count;
} load_scan;

// "<slot>.<digits>.anosave<anything>.tmp": one of OUR protocol temps that never
// renamed. Strict digits+suffix so slot "save" never claims "save.backup.3..."'s
// temps (dotted-prefix slot names must not cross-match).
static bool save_tmp_name(const char *name, const char *slot, size_t slot_len)
{
    size_t nlen = strlen(name);
    if (nlen < slot_len + 1 + 1 + 8 + 4 || nlen >= MAXPATH)
        return false;
    if (memcmp(name, slot, slot_len) != 0 || name[slot_len] != '.')
        return false;
    if (strcmp(name + nlen - 4, ".tmp") != 0)
        return false;
    const char *p = name + slot_len + 1;
    if (*p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9'))
        return false;
    while (*p >= '0' && *p <= '9')
        p++;
    return memcmp(p, ".anosave", 8) == 0;
}

static void load_scan_cb(const char *name, void *ctx)
{
    load_scan *s = ctx;
    uint64_t seq;
    if (res_save_name_seq(name, s->slot, s->slot_len, &seq)) {
        // Sorted-descending insertion, capacity 64: the NEWEST generations always
        // survive the scan, whatever order readdir walks in.
        int cap = (int)(sizeof s->seqs / sizeof s->seqs[0]);
        int i = s->count < cap ? s->count : cap;
        while (i > 0 && s->seqs[i - 1] < seq) {
            if (i < cap)
                s->seqs[i] = s->seqs[i - 1];
            i--;
        }
        if (i < cap) {
            s->seqs[i] = seq;
            if (s->count < cap)
                s->count++;
        }
        return;
    }
    if (save_tmp_name(name, s->slot, s->slot_len)
        && s->tmp_count < (int)(sizeof s->tmps / sizeof s->tmps[0]))
        memcpy(s->tmps[s->tmp_count++], name, strlen(name) + 1);
}

// Validate one absolute candidate; on success install it as "saves/<slot>" and emit the
// handle. rc: 0 won, -1 keep trying.
static int save_try_file(const char *abs, const char *slot, size_t slot_len,
                         uint64_t want_seq, bool check_seq,
                         anores_t *out, uint32_t *out_fmt, uint64_t *out_seq)
{
    void  *bytes = NULL;
    size_t blen  = 0;
    if (res_read_all(NULL, abs, &bytes, &blen) != 0)
        return -1;
    uint32_t fmt;
    uint64_t fseq;
    const uint8_t *payload;
    size_t plen;
    int v = res_save_validate(bytes, blen, &fmt, &fseq, &payload, &plen);
    if (v != 0) {
        mi_free(bytes);
        ano_log(ANO_WARN, "resources: save %s failed validation (%s damage), "
                          "degrading one generation",
                abs, v == -1 ? "header" : "body");
        return -1;
    }
    if (check_seq && fseq != want_seq) {
        mi_free(bytes);
        ano_log(ANO_WARN, "resources: save %s frame seq %llu does not echo its "
                          "filename (rename masquerade?), skipping",
                abs, (unsigned long long)fseq);
        return -1;
    }

    // Own it as "saves/<slot>": copy the payload out of the frame into placement.
    char logical[MAXPATH];
    int w = snprintf(logical, sizeof logical, "saves/%s", slot);
    if (w < 0 || w >= (int)sizeof logical) {
        mi_free(bytes);
        return -1;
    }
    uint64_t rid = res_fnv1a64(logical, (size_t)w);
    ano_mutex_lock(&g_reg.mtx);
    uint32_t slot_idx = row_bind(rid, logical, (size_t)w);
    if (slot_idx == UINT32_MAX) {
        ano_mutex_unlock(&g_reg.mtx);
        mi_free(bytes);
        return -1;
    }
    res_row *row = &g_reg.rows[slot_idx];
    if (row->data != NULL) {                    // an older loaded generation retires
        payload_free(row);
        row->gen += 1;
    }
    bool direct;
    void *home = payload_alloc(plen, &direct);
    if (home == NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        mi_free(bytes);
        return -1;
    }
    memcpy(home, payload, plen);
    ((uint8_t *)home)[plen] = 0;
    row->data   = home;
    row->size   = plen;
    row->direct = direct;
    row->gen   += 1;
    *out = (anores_t){ rid, slot_idx, row->gen };
    ano_mutex_unlock(&g_reg.mtx);
    mi_free(bytes);
    if (out_fmt) *out_fmt = fmt;
    if (out_seq) *out_seq = fseq;
    (void)slot_len;
    return 0;
}

anores_t ano_res_save_load(const char *slot, uint32_t *format_version, uint64_t *seq)
{
    anores_t none = {0};
    size_t slot_len;
    if (res_segment_validate(slot, &slot_len) != 0) {
        ano_log(ANO_ERROR, "resources: save load refused (bad slot)");
        return none;
    }
    if (!res_ready()) {
        ano_log(ANO_ERROR, "resources: save load before init");
        return none;
    }
    res_freeze();
    ano_fspath wroot = res_write_root();
    ano_fspath saves = res_join(&wroot, "saves", 5);
    if (saves.length == 0)
        return none;

    res_save_lock();                            // serialize against commits
    load_scan scan = { .slot = slot, .slot_len = slot_len };
    if (rmos_scan_dir(saves.str, load_scan_cb, &scan) != 0) {
        // Absent dir = a legitimately fresh install; either way nothing is readable.
        res_save_unlock();
        ano_log(ANO_WARN, "resources: saves dir unreadable or absent for slot %s, "
                          "starting fresh", slot);
        return none;
    }

    anores_t won = {0};                         // seqs[] arrives newest-first
    for (int i = 0; i < scan.count && won.gen == 0; i++) {
        char rel[MAXPATH];
        int w = snprintf(rel, sizeof rel, "saves/%s.%llu.anosave", slot,
                         (unsigned long long)scan.seqs[i]);
        if (w < 0 || w >= (int)sizeof rel)
            continue;
        ano_fspath abs = res_join(&wroot, rel, (size_t)w);
        if (abs.length == 0)
            continue;
        save_try_file(abs.str, slot, slot_len, scan.seqs[i], true,
                      &won, format_version, seq);
    }
    // Orphaned temps: tried last. A VALID one is the interrupted protocol's payload --
    // COMPLETE the crashed rename (unlinking would delete the only on-disk copy);
    // everything else purges.
    for (int i = 0; i < scan.tmp_count; i++) {
        ano_fspath abs = res_join(&saves, scan.tmps[i], strlen(scan.tmps[i]));
        if (abs.length == 0)
            continue;
        if (won.gen == 0) {
            uint32_t tfmt = 0;
            uint64_t tseq = 0;
            if (save_try_file(abs.str, slot, slot_len, 0, false, &won, &tfmt, &tseq) == 0) {
                char rel[MAXPATH];
                int w = snprintf(rel, sizeof rel, "saves/%s.%llu.anosave", slot,
                                 (unsigned long long)tseq);
                ano_fspath dst = w > 0 && w < (int)sizeof rel
                               ? res_join(&wroot, rel, (size_t)w) : (ano_fspath){0};
                // If a file of that name exists it just FAILED validation above;
                // replacing it with a frame that validates is strictly better.
                if (dst.length != 0 && rmos_rename_replace(abs.str, dst.str) == 0) {
                    rmos_sync_dir(saves.str);
                    ano_log(ANO_WARN, "resources: recovered orphan temp as %s", rel);
                } else {
                    rmos_unlink(abs.str);
                }
                if (format_version) *format_version = tfmt;
                if (seq)            *seq            = tseq;
                continue;
            }
        }
        rmos_unlink(abs.str);
    }
    res_save_unlock();

    if (won.gen == 0)
        ano_log(ANO_WARN, "resources: no valid save generation for slot %s, "
                          "starting fresh", slot);
    return won;
}
