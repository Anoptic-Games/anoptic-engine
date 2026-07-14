/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Synchronous Stage A registry. Mutable maps, rows, domains, and accounting remain under
// one mutex. The read side does not: a fixed permanent slot directory release-publishes
// immutable descriptors and readers acquire them inside registered epoch scopes. Rows live
// in non-moving chunks. Owner/domain and row generation wrap refuse rather than alias.

#include <anoptic_resources.h>

#include <anoptic_log.h>
#include <anoptic_threads.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resources_ext.h"
#include "resources_internal.h"
#include "resources_place.h"
#include "resources_tel.h"

// The graphics extension's registration hook (res_graphics.c). An extern here rather than
// a header: every .h under src/resources/ is frozen at W0, and the roster call moves into
// the W6 graphics split (res_gfx_ext.c) with the rest of the extension surface.
extern void res_gfx_register_ext(void);

#define RES_SLOT_MAX       4096u
#define RES_ROW_CHUNK      64u
#define RES_ROW_CHUNKS     (RES_SLOT_MAX / RES_ROW_CHUNK)
#define RES_DOMAIN_MAX     32u
#define RES_READER_MAX     64u
#define RES_QUIESCENT      UINT64_MAX

static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "resource publication requires lock-free pointers");
static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "resource epochs require lock-free 64-bit atomics");

typedef struct res_pub {
    uint64_t rid;
    const void *data;
    size_t size;
    uint32_t slot;
    uint32_t generation;
    uint32_t owner;
    uint32_t owner_generation;
    uint32_t tag;
    uint32_t dependency_count;
} res_pub;

typedef struct res_row {
    uint64_t rid;
    char *name;
    uint32_t name_len;
    uint32_t generation;
    res_owned_block payload;
    res_owned_block name_storage;
    res_owned_block dependency_storage;
    size_t dependency_count;
    bool bound;
    bool generation_exhausted;
} res_row;

typedef enum res_domain_state {
    RES_DOMAIN_FREE = 0,
    RES_DOMAIN_LIVE,
    RES_DOMAIN_RETIRING,
} res_domain_state;

// M5: the domain no longer owns a heap or a pool -- memory ownership lives in the
// placement root keyed by the owner index. The registry keeps identity and teardown state.
typedef struct res_domain {
    ano_res_lifetime_kind kind;
    uint32_t generation;
    res_domain_state state;
    size_t live_rows;
    size_t pending_blocks;
} res_domain;

typedef struct res_retired {
    struct res_retired *next;
    res_pub *pub;
    res_owned_block block;
    uint64_t epoch;
} res_retired;

typedef struct res_reader_lane {
    _Atomic uint32_t cookie;
    _Atomic uint64_t epoch;
} res_reader_lane;

// Reader lanes are stripe-backed (ano_mem_stripe's first production consumer): one 16-byte
// lane per reader thread, each on its own ANO_THREAD_LINE granule. The old
// `res_reader_lane g_readers[64]` packed four raced lanes per cache line, so every reader's
// epoch store false-shared with three neighbors across 1.2M+ observations per race run.
// Pointers publish with release at init and become NULL at shutdown; a stale reader handle
// observes NULL and refuses instead of touching reclaimed memory.
static _Atomic(res_pub *) g_directory[RES_SLOT_MAX];
static _Atomic(res_reader_lane *) g_readers[RES_READER_MAX];
static ano_mem_stripe *g_reader_stripe;
static _Atomic uint64_t g_epoch = 1;
static _Atomic uint32_t g_reader_cookie = 1;
static uint32_t g_owner_generations[RES_DOMAIN_MAX];
static uint32_t g_row_seed;
static uint32_t g_row_gen_high;                 // highest generation ever PUBLISHED, across inits

static struct {
    anothread_mutex_t mtx;
    anothread_t owner_thread;
    _Atomic bool alive;                         // read by reader registration off-thread
    uint32_t *slots;
    res_owned_block slots_storage;
    uint32_t slot_mask;
    res_row *row_chunks[RES_ROW_CHUNKS];
    res_owned_block row_storage[RES_ROW_CHUNKS];
    uint32_t row_count;
    res_domain domains[RES_DOMAIN_MAX];
    res_retired *retired;
    ano_res_allocator_stats stats;
} g_reg;

static inline bool reg_alive(void)
{
    return atomic_load_explicit(&g_reg.alive, memory_order_acquire);
}

static bool owner_thread(void)
{
    return reg_alive() && ano_thread_equal(g_reg.owner_thread, ano_thread_self());
}

static res_row *row_at(uint32_t slot)
{
    if (slot >= g_reg.row_count)
        return NULL;
    res_row *chunk = g_reg.row_chunks[slot / RES_ROW_CHUNK];
    return chunk ? &chunk[slot % RES_ROW_CHUNK] : NULL;
}

static res_domain *domain_of(ano_res_lifetime lifetime, bool live_only)
{
    if (lifetime.owner == 0 || lifetime.owner > RES_DOMAIN_MAX)
        return NULL;
    res_domain *d = &g_reg.domains[lifetime.owner - 1];
    if (d->generation != lifetime.generation || d->kind != lifetime.kind)
        return NULL;
    if (live_only && d->state != RES_DOMAIN_LIVE)
        return NULL;
    if (!live_only && d->state == RES_DOMAIN_FREE)
        return NULL;
    return d;
}

// The seam (M5). The domain check stays HERE: the placement root table is keyed by owner
// index alone, so a stale lifetime (dead generation, wrong kind) must be refused by the
// registry before the route ever sees the key. serving_size() and legacy_site_flags() are
// DELETED -- the site carries serving/alignment/flags and free() never recomputes them.
static int owned_alloc_locked(const res_place_plan *plan, size_t size, res_owned_block *out)
{
    res_domain *d = domain_of(plan->lifetime, true);
    if (d == NULL)
        return -1;
    size_t bytes = size + 1;                    // guard NUL, exactly as before
    res_site site;
    if (res_place_route(plan, bytes, &site) != 0)
        return -1;
    void *p = res_place_alloc(&site, bytes);
    if (p == NULL)
        return -1;
    *out = (res_owned_block){ .data = p, .size = size, .site = site, .plan = *plan };
    g_reg.stats.requested_bytes += size;
    g_reg.stats.serving_bytes += site.serving;
    g_reg.stats.live_bytes += site.serving;
    g_reg.stats.live_blocks++;
    g_reg.stats.allocations++;
    if (g_reg.stats.live_bytes > g_reg.stats.peak_bytes)
        g_reg.stats.peak_bytes = g_reg.stats.live_bytes;
    if (g_reg.stats.live_blocks > g_reg.stats.peak_blocks)
        g_reg.stats.peak_blocks = g_reg.stats.live_blocks;
    return 0;
}

// Inputs: a live destination plan and requested payload bytes. Output: one unpublished
// owned block. Invariant: backing heaps and pools are touched only by the init/owner thread.
int res_owned_alloc(const res_place_plan *plan, size_t size, res_owned_block *out)
{
    if (plan == NULL || out == NULL || size == SIZE_MAX || !owner_thread())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    int rc = owned_alloc_locked(plan, size, out);
    ano_mutex_unlock(&g_reg.mtx);
    return rc;
}

// The block's site says where it lives; nobody re-derives the pool from the domain. The
// pool outlives every block routed into it: a domain stays RETIRING (root un-winked) while
// pending_blocks > 0, so this free never races the root teardown.
static void block_free_locked(res_owned_block *block)
{
    if (block == NULL || block->data == NULL)
        return;
    res_place_free(&block->site, block->data, block->size + 1, RES_FREE_RETAIL);
    g_reg.stats.live_bytes -= block->site.serving;
    g_reg.stats.live_blocks--;
    g_reg.stats.frees++;
    *block = (res_owned_block){0};
}

// mode is honored once sites can be winkable. TODO(W1, M6): RES_FREE_WINK becomes
// accounting-only on a RES_SITE_WINKABLE site and real work everywhere else -- one code
// path, five models, no asymmetric bookkeeping charge (D11).
void res_owned_free(res_owned_block *block, res_free_mode mode)
{
    (void)mode;
    if (block == NULL || block->data == NULL || !owner_thread())
        return;
    ano_mutex_lock(&g_reg.mtx);
    block_free_locked(block);
    ano_mutex_unlock(&g_reg.mtx);
}

// The pure half of res_owned_alloc: same domain gate, same route, no allocation.
int res_owned_plan(const res_place_plan *plan, size_t size, res_site *out)
{
    if (plan == NULL || out == NULL || size == SIZE_MAX || !owner_thread())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    int rc = domain_of(plan->lifetime, true) != NULL
           ? res_place_route(plan, size + 1, out) : -1;
    ano_mutex_unlock(&g_reg.mtx);
    return rc;
}

int res_owned_stage(const res_place_plan *plan, size_t hint, res_owned_block *out)
{
    (void)plan; (void)hint; (void)out;
    return -1;                                  // TODO(W2, M10): monotonic staging
}

int res_owned_commit(res_owned_block *staged, const res_place_plan *home, res_owned_block *out)
{
    (void)staged; (void)home; (void)out;
    return -1;                                  // TODO(W2, M10): staging -> planned home
}

int res_owned_move(res_owned_block *from, const res_place_plan *to, res_owned_block *out)
{
    (void)from; (void)to; (void)out;
    return -1;                                  // TODO(W2, M8): promote / duplicate ride this
}

// M3: the plan is no longer discarded -- the five-axis cube charges the plan's cell
// alongside the module-wide totals.
void res_account_copy(const res_place_plan *plan, size_t bytes)
{
    if (!reg_alive())
        return;
    ano_mutex_lock(&g_reg.mtx);
    g_reg.stats.copies++;
    g_reg.stats.bytes_copied += bytes;
    res_tel_copy(res_tel_intern(plan), bytes);
    ano_mutex_unlock(&g_reg.mtx);
}

void res_account_transfer(const res_place_plan *plan, size_t bytes)
{
    if (!reg_alive())
        return;
    ano_mutex_lock(&g_reg.mtx);
    g_reg.stats.transfers++;
    g_reg.stats.transfer_bytes += bytes;
    res_tel_transfer(res_tel_intern(plan), bytes, true);
    ano_mutex_unlock(&g_reg.mtx);
}

void res_registry_external_allocation(const res_place_plan *plan, size_t bytes)
{
    if (plan == NULL || !reg_alive())
        return;
    ano_mutex_lock(&g_reg.mtx);
    g_reg.stats.requested_bytes += bytes;
    g_reg.stats.serving_bytes += bytes;
    g_reg.stats.allocations++;
    g_reg.stats.transfers++;
    g_reg.stats.transfer_bytes += bytes;
    uint16_t cell = res_tel_intern(plan);
    res_tel_alloc(cell, bytes, bytes);          // charged and never reversed: the M9 wound
    res_tel_transfer(cell, bytes, true);
    ano_mutex_unlock(&g_reg.mtx);
}

int res_disposition_allowed(ano_res_lifetime from, ano_res_lifetime to,
                            res_disposition disposition, bool transfer_compatible)
{
    if (from.owner == 0 || to.owner == 0)
        return -1;
    switch (disposition) {
    case RES_DISPOSITION_RETAIN:
        return from.owner == to.owner && from.generation == to.generation ? 0 : -1;
    case RES_DISPOSITION_PROMOTE:
        if (to.kind != ANO_RES_LIFETIME_ENGINE
            && !(from.kind == ANO_RES_LIFETIME_TRANSIENT_IMPORT
                 && to.kind == ANO_RES_LIFETIME_TOOL_IMPORT))
            return -1;
        if (reg_alive()) {
            ano_mutex_lock(&g_reg.mtx); g_reg.stats.promotions++; ano_mutex_unlock(&g_reg.mtx);
        }
        return 0;
    case RES_DISPOSITION_DUPLICATE:
        if (reg_alive()) {
            ano_mutex_lock(&g_reg.mtx); g_reg.stats.duplications++; ano_mutex_unlock(&g_reg.mtx);
        }
        return 0;
    case RES_DISPOSITION_TRANSFER:
        return transfer_compatible ? 0 : -1;
    default:
        return -1;
    }
}

static uint32_t probe_find(uint64_t rid)
{
    uint32_t idx = (uint32_t)rid & g_reg.slot_mask;
    while (g_reg.slots[idx] != 0) {
        uint32_t slot = g_reg.slots[idx] - 1;
        res_row *row = row_at(slot);
        if (row != NULL && row->rid == rid)
            return slot;
        idx = (idx + 1) & g_reg.slot_mask;
    }
    return UINT32_MAX;
}

static void slot_insert(uint64_t rid, uint32_t slot)
{
    uint32_t idx = (uint32_t)rid & g_reg.slot_mask;
    while (g_reg.slots[idx] != 0)
        idx = (idx + 1) & g_reg.slot_mask;
    g_reg.slots[idx] = slot + 1;
}

static res_place_plan root_plan(res_role role);

static int grow_slots(void)
{
    uint32_t old_cap = g_reg.slot_mask + 1;
    if (old_cap >= RES_SLOT_MAX * 2)
        return -1;
    res_place_plan plan = root_plan(RES_ROLE_REGISTRY);
    res_owned_block storage = {0};
    size_t bytes = (size_t)old_cap * 2 * sizeof(uint32_t);
    if (owned_alloc_locked(&plan, bytes, &storage) != 0)
        return -1;
    memset(storage.data, 0, bytes);
    res_owned_block old = g_reg.slots_storage;
    g_reg.slots_storage = storage;
    g_reg.slots = storage.data;
    g_reg.slot_mask = old_cap * 2 - 1;
    for (uint32_t i = 0; i < g_reg.row_count; i++) {
        res_row *row = row_at(i);
        if (row && row->bound)
            slot_insert(row->rid, i);
    }
    block_free_locked(&old);
    return 0;
}

static res_place_plan root_plan(res_role role)
{
    return (res_place_plan){
        .tag = RES_TAG_BYTES,
        .lifetime = ano_res_lifetime_engine(),
        .role = role,
        .operation = RES_OP_BIND,
        .destination = RES_DEST_METADATA,
        .provenance = RES_PROVENANCE_NAMESPACE,
        .alignment = _Alignof(max_align_t),
    };
}

static int root_block_alloc(res_role role, size_t size, res_owned_block *out)
{
    res_place_plan plan = root_plan(role);
    return res_owned_alloc(&plan, size, out);
}

static int root_block_alloc_locked(res_role role, size_t size, res_owned_block *out)
{
    res_place_plan plan = root_plan(role);
    return owned_alloc_locked(&plan, size, out);
}

// Find or permanently bind one non-moving row. Names live in the root metadata domain so
// collision diagnostics survive retirement of the payload's lifetime domain.
static uint32_t row_bind(uint64_t rid, const char *logical, size_t len)
{
    uint32_t slot = probe_find(rid);
    if (slot != UINT32_MAX) {
        res_row *row = row_at(slot);
        if (row->name_len != (uint32_t)len || memcmp(row->name, logical, len) != 0) {
            ano_log(ANO_ERROR, "resources: rid collision: '%s' vs '%s'", row->name, logical);
            return UINT32_MAX;
        }
        return slot;
    }
    if (g_reg.row_count >= RES_SLOT_MAX)
        return UINT32_MAX;
    if ((uint64_t)(g_reg.row_count + 1) * 10 > (uint64_t)(g_reg.slot_mask + 1) * 7)
        if (grow_slots() != 0)
            return UINT32_MAX;
    slot = g_reg.row_count;
    uint32_t ci = slot / RES_ROW_CHUNK;
    if (g_reg.row_chunks[ci] == NULL) {
        res_place_plan plan = root_plan(RES_ROLE_REGISTRY);
        size_t bytes = RES_ROW_CHUNK * sizeof(res_row);
        if (owned_alloc_locked(&plan, bytes, &g_reg.row_storage[ci]) != 0)
            return UINT32_MAX;
        memset(g_reg.row_storage[ci].data, 0, bytes);
        g_reg.row_chunks[ci] = g_reg.row_storage[ci].data;
    }
    res_owned_block name = {0};
    if (root_block_alloc_locked(RES_ROLE_NAME, len, &name) != 0)
        return UINT32_MAX;
    memcpy(name.data, logical, len);
    ((char *)name.data)[len] = '\0';
    res_row *row = &g_reg.row_chunks[ci][slot % RES_ROW_CHUNK];
    *row = (res_row){
        .rid = rid, .name = name.data, .name_len = (uint32_t)len,
        .generation = g_row_seed, .name_storage = name, .bound = true,
    };
    g_reg.row_count++;
    g_reg.stats.rows_bound = g_reg.row_count;
    slot_insert(rid, slot);
    return slot;
}

static res_row *row_live(anores_t h)
{
    res_row *row = row_at(h.slot);
    if (row == NULL || !row->bound || row->rid != h.rid
        || row->generation != h.gen || row->payload.data == NULL)
        return NULL;
    return row;
}

// The lane for slot i, or NULL outside the init..shutdown window. Acquire pairs with the
// release publication in res_registry_init.
static inline res_reader_lane *reader_lane_at(uint32_t i)
{
    return atomic_load_explicit(&g_readers[i], memory_order_acquire);
}

static bool reader_safe(uint64_t retired_epoch, size_t *stalled)
{
    bool safe = true;
    for (uint32_t i = 0; i < RES_READER_MAX; i++) {
        res_reader_lane *lane = reader_lane_at(i);
        if (lane == NULL || atomic_load_explicit(&lane->cookie, memory_order_acquire) == 0)
            continue;
        // seq_cst pairs with the seq_cst epoch publication in ano_res_read_begin: without a
        // single total order this load and the reader's store could miss each other
        // (store-buffering) and a pinned payload would be reclaimed under a live read scope.
        uint64_t e = atomic_load_explicit(&lane->epoch, memory_order_seq_cst);
        if (e != RES_QUIESCENT && e <= retired_epoch) {
            safe = false;
            if (stalled) (*stalled)++;
        }
    }
    return safe;
}

static void queue_retire_locked(res_retired *r, res_pub *pub, res_owned_block block)
{
    uint64_t epoch = atomic_fetch_add_explicit(&g_epoch, 1, memory_order_seq_cst) + 1;
    *r = (res_retired){ .next = g_reg.retired, .pub = pub, .block = block, .epoch = epoch };
    g_reg.retired = r;
    g_reg.stats.retired_pending++;
    res_domain *d = domain_of(block.plan.lifetime, false);
    if (d) d->pending_blocks++;
}

static void finish_domains_locked(void)
{
    for (uint32_t i = 1; i < RES_DOMAIN_MAX; i++) {
        res_domain *d = &g_reg.domains[i];
        if (d->state != RES_DOMAIN_RETIRING || d->live_rows != 0 || d->pending_blocks != 0)
            continue;
        res_place_domain_wink((ano_res_lifetime){
            .owner = i + 1, .generation = d->generation, .kind = d->kind });
        d->state = RES_DOMAIN_FREE;
        if (g_reg.stats.domains_live > 0)
            g_reg.stats.domains_live--;
    }
}

static size_t collect_locked(void)
{
    size_t reclaimed = 0;
    size_t stalled = 0;
    res_retired **link = &g_reg.retired;
    while (*link != NULL) {
        res_retired *r = *link;
        size_t local_stalled = 0;
        if (!reader_safe(r->epoch, &local_stalled)) {
            stalled += local_stalled;
            link = &r->next;
            continue;
        }
        *link = r->next;
        res_domain *d = domain_of(r->block.plan.lifetime, false);
        block_free_locked(&r->block);
        if (d && d->pending_blocks > 0)
            d->pending_blocks--;
        if (r->pub) {
            mi_free(r->pub);
            g_reg.stats.descriptors_live--;
        }
        mi_free(r);
        g_reg.stats.retired_pending--;
        reclaimed++;
    }
    g_reg.stats.stalled_readers += stalled;
    finish_domains_locked();
    return reclaimed;
}

size_t ano_res_collect(void)
{
    if (!owner_thread())
        return 0;
    ano_mutex_lock(&g_reg.mtx);
    size_t n = collect_locked();
    ano_mutex_unlock(&g_reg.mtx);
    return n;
}

static int publish_locked(uint32_t slot, res_row *row)
{
    if (row->generation_exhausted || row->generation == UINT32_MAX)
        return -1;
    row->generation++;
    if (row->generation > g_row_gen_high)
        g_row_gen_high = row->generation;       // handles never validate across a reinit
    res_pub *pub = mi_malloc(sizeof *pub);
    if (pub == NULL)
        return -1;
    *pub = (res_pub){
        .rid = row->rid, .data = row->payload.data, .size = row->payload.size,
        .slot = slot, .generation = row->generation,
        .owner = row->payload.plan.lifetime.owner,
        .owner_generation = row->payload.plan.lifetime.generation,
        .tag = row->payload.plan.tag,
        .dependency_count = (uint32_t)row->dependency_count,
    };
    atomic_store_explicit(&g_directory[slot], pub, memory_order_release);
    g_reg.stats.descriptors_live++;
    res_domain *d = domain_of(row->payload.plan.lifetime, true);
    if (d) d->live_rows++;
    return 0;
}

static int retire_locked(uint32_t slot, res_row *row)
{
    res_pub *pub = atomic_load_explicit(&g_directory[slot], memory_order_acquire);
    if (pub == NULL)
        return -1;
    res_retired *retired = mi_malloc(sizeof *retired);
    if (retired == NULL)
        return -1;
    atomic_store_explicit(&g_directory[slot], NULL, memory_order_release);
    res_owned_block block = row->payload;
    row->payload = (res_owned_block){0};
    res_domain *d = domain_of(block.plan.lifetime, false);
    if (d && d->live_rows > 0) d->live_rows--;
    if (row->generation == UINT32_MAX) {
        row->generation_exhausted = true;
    } else {
        row->generation++;
        if (row->generation == UINT32_MAX)
            row->generation_exhausted = true;
    }
    if (row->dependency_storage.data != NULL) {
        block_free_locked(&row->dependency_storage);
        row->dependency_count = 0;
    }
    queue_retire_locked(retired, pub, block);
    return 0;
}

// Inputs: none. Output: 0 on complete construction. Invariant: the calling thread becomes
// the synchronous Stage A owner; extensions register and freeze, telemetry and placement
// come up, and every backing heap is created THROUGH the placement seam.
int res_registry_init(void)
{
    memset(&g_reg, 0, sizeof g_reg);
    if (ano_mutex_init(&g_reg.mtx, NULL) != 0)
        return -1;
    g_reg.owner_thread = ano_thread_self();
    atomic_store_explicit(&g_reg.alive, true, memory_order_release);
    bool place_live = false;
    res_domain *root = &g_reg.domains[0];
    // The seed must clear every generation the LAST init ever published, not just the last
    // seed: a busy run pushes row generations far past seed + 2, and a surviving {rid, slot,
    // gen} handle would validate against the reborn registry.
    if (g_row_gen_high >= UINT32_MAX - 2)
        goto fail;
    if (g_row_seed < g_row_gen_high)
        g_row_seed = g_row_gen_high + (g_row_gen_high & 1u);
    if (g_row_seed > UINT32_MAX - 2)
        goto fail;
    g_row_seed += 2;
    for (uint32_t i = 0; i < RES_SLOT_MAX; i++)
        atomic_store_explicit(&g_directory[i], NULL, memory_order_relaxed);
    if (g_owner_generations[0] == UINT32_MAX)
        goto fail;
    // M2: MEANING first. The core owns only RES_TAG_BYTES (dense id 0 by construction);
    // the extensions register their kinds, then the set freezes -- sorted by fourcc, so
    // dense ids are a function of the tag set, never of this call order (D17).
    res_ext_reset();
    res_gfx_register_ext();
    res_ext_freeze();
    if (res_tel_init() != 0)
        goto fail;
    // M5: the placement scaffold, chosen once per init (7.1). getenv is read here and
    // nowhere else; the INFO line is logged by res_place_init.
    if (res_place_init(getenv("ANO_RES_PLACEMENT")) != 0)
        goto fail;
    place_live = true;
    root->generation = ++g_owner_generations[0];
    root->kind = ANO_RES_LIFETIME_ENGINE;
    root->state = RES_DOMAIN_LIVE;
    if (res_place_domain_open((ano_res_lifetime){
            .owner = 1, .generation = root->generation,
            .kind = ANO_RES_LIFETIME_ENGINE }) != 0)
        goto fail;
    // Grain-isolated reader lanes: 64 lanes, one ANO_THREAD_LINE granule each. Registry
    // infrastructure, not domain memory: parented on the process default heap, destroyed
    // explicitly at shutdown and on this fail path.
    ano_mem_stripe_cfg lane_cfg = {
        .lanes = RES_READER_MAX, .grain = 0, .chunk_hint = ANO_THREAD_LINE,
    };
    g_reader_stripe = ano_mem_stripe_make(ano_mem_parent_default(), &lane_cfg);
    if (g_reader_stripe == NULL)
        goto fail;
    for (uint32_t i = 0; i < RES_READER_MAX; i++) {
        res_reader_lane *lane = ano_mem_stripe_alloc(g_reader_stripe, i, sizeof *lane, 0);
        if (lane == NULL)
            goto fail;
        atomic_store_explicit(&lane->cookie, 0, memory_order_relaxed);
        atomic_store_explicit(&lane->epoch, RES_QUIESCENT, memory_order_relaxed);
        atomic_store_explicit(&g_readers[i], lane, memory_order_release);
    }
    res_place_plan registry_plan = root_plan(RES_ROLE_REGISTRY);
    size_t registry_bytes = 64u * sizeof *g_reg.slots;
    if (owned_alloc_locked(&registry_plan, registry_bytes, &g_reg.slots_storage) != 0)
        goto fail;
    memset(g_reg.slots_storage.data, 0, registry_bytes);
    g_reg.slots = g_reg.slots_storage.data;
    g_reg.slot_mask = 63u;
    g_reg.stats.domains_live = 1;
    return 0;
fail:
    for (uint32_t i = 0; i < RES_READER_MAX; i++)
        atomic_store_explicit(&g_readers[i], NULL, memory_order_release);
    if (g_reader_stripe != NULL) {
        ano_mem_stripe_destroy(g_reader_stripe);
        g_reader_stripe = NULL;
    }
    if (place_live)
        res_place_shutdown();
    res_tel_shutdown();
    res_ext_reset();
    atomic_store_explicit(&g_reg.alive, false, memory_order_release);
    ano_mutex_destroy(&g_reg.mtx);
    return -1;
}

ano_res_lifetime ano_res_lifetime_engine(void)
{
    if (!reg_alive())
        return (ano_res_lifetime){0};
    return (ano_res_lifetime){
        .owner = 1, .generation = g_reg.domains[0].generation,
        .kind = ANO_RES_LIFETIME_ENGINE,
    };
}

int ano_res_domain_open(ano_res_lifetime_kind kind, ano_res_lifetime *out)
{
    if (out == NULL || kind <= ANO_RES_LIFETIME_ENGINE
        || kind > ANO_RES_LIFETIME_TOOL_IMPORT || !owner_thread())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    collect_locked();
    for (uint32_t i = 1; i < RES_DOMAIN_MAX; i++) {
        res_domain *d = &g_reg.domains[i];
        if (d->state != RES_DOMAIN_FREE)
            continue;
        if (g_owner_generations[i] == UINT32_MAX) {
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
        d->generation = ++g_owner_generations[i];
        d->kind = kind;
        ano_res_lifetime lt = { .owner = i + 1, .generation = d->generation, .kind = kind };
        if (res_place_domain_open(lt) != 0) {
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
        d->state = RES_DOMAIN_LIVE;
        *out = lt;
        g_reg.stats.domains_live++;
        ano_mutex_unlock(&g_reg.mtx);
        return 0;
    }
    ano_mutex_unlock(&g_reg.mtx);
    return -1;
}

int ano_res_domain_retire(ano_res_lifetime lifetime)
{
    if (!owner_thread() || lifetime.kind == ANO_RES_LIFETIME_ENGINE)
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    // live_only == false: a retire that failed mid-scan (retirement-record OOM) leaves the
    // domain RETIRING, and the retry must be able to find it again.
    res_domain *d = domain_of(lifetime, false);
    if (d == NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    d->state = RES_DOMAIN_RETIRING;
    for (uint32_t i = 0; i < g_reg.row_count; i++) {
        res_row *row = row_at(i);
        if (row->payload.data == NULL
            || row->payload.plan.lifetime.owner != lifetime.owner
            || row->payload.plan.lifetime.generation != lifetime.generation)
            continue;
        if (retire_locked(i, row) != 0) {
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
    }
    collect_locked();
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

int ano_res_reader_register(ano_res_reader *reader)
{
    if (reader == NULL || !reg_alive())
        return -1;
    for (uint32_t i = 0; i < RES_READER_MAX; i++) {
        res_reader_lane *lane = reader_lane_at(i);
        if (lane == NULL)
            return -1;
        uint32_t expect = 0;
        uint32_t cookie = atomic_fetch_add_explicit(&g_reader_cookie, 1, memory_order_relaxed) + 1;
        if (cookie == 0)
            return -1;
        if (atomic_compare_exchange_strong_explicit(&lane->cookie, &expect, cookie,
                                                    memory_order_acq_rel, memory_order_relaxed)) {
            atomic_store_explicit(&lane->epoch, RES_QUIESCENT, memory_order_release);
            *reader = (ano_res_reader){ .lane = i, .cookie = cookie };
            return 0;
        }
    }
    return -1;
}

int ano_res_reader_unregister(ano_res_reader *reader)
{
    if (reader == NULL || reader->lane >= RES_READER_MAX || reader->cookie == 0)
        return -1;
    res_reader_lane *lane = reader_lane_at(reader->lane);
    if (lane == NULL)
        return -1;
    if (atomic_load_explicit(&lane->cookie, memory_order_acquire) != reader->cookie
        || atomic_load_explicit(&lane->epoch, memory_order_acquire) != RES_QUIESCENT)
        return -1;
    uint32_t expect = reader->cookie;
    if (!atomic_compare_exchange_strong_explicit(&lane->cookie, &expect, 0,
                                                 memory_order_acq_rel, memory_order_relaxed))
        return -1;
    *reader = (ano_res_reader){ .lane = ANO_RES_READER_NONE };
    return 0;
}

int ano_res_read_begin(ano_res_reader *reader, ano_res_read *read)
{
    if (reader == NULL || read == NULL || reader->lane >= RES_READER_MAX || reader->cookie == 0)
        return -1;
    res_reader_lane *lane = reader_lane_at(reader->lane);
    if (lane == NULL)
        return -1;
    if (atomic_load_explicit(&lane->cookie, memory_order_acquire) != reader->cookie
        || atomic_load_explicit(&lane->epoch, memory_order_acquire) != RES_QUIESCENT)
        return -1;
    // Publish-then-revalidate under seq_cst. If the owner's retire (epoch bump, then lane
    // scan) misses this store, the single total order forces the re-load below to see the
    // bumped counter and the epoch re-publishes before the scope is trusted -- the
    // store-buffering window where a reader pins an epoch the owner already swept past
    // cannot happen.
    uint64_t epoch = atomic_load_explicit(&g_epoch, memory_order_seq_cst);
    for (;;) {
        atomic_store_explicit(&lane->epoch, epoch, memory_order_seq_cst);
        uint64_t now = atomic_load_explicit(&g_epoch, memory_order_seq_cst);
        if (now == epoch)
            break;
        epoch = now;
    }
    *read = (ano_res_read){ .reader = reader, .epoch = epoch };
    return 0;
}

void ano_res_read_end(ano_res_read *read)
{
    if (read == NULL || read->reader == NULL || read->reader->lane >= RES_READER_MAX)
        return;
    res_reader_lane *lane = reader_lane_at(read->reader->lane);
    if (lane != NULL
        && atomic_load_explicit(&lane->cookie, memory_order_acquire) == read->reader->cookie)
        atomic_store_explicit(&lane->epoch, RES_QUIESCENT, memory_order_release);
    *read = (ano_res_read){0};
}

static bool read_active(const ano_res_read *read)
{
    if (read == NULL || read->reader == NULL || read->reader->lane >= RES_READER_MAX)
        return false;
    res_reader_lane *lane = reader_lane_at(read->reader->lane);
    return lane != NULL
        && atomic_load_explicit(&lane->cookie, memory_order_acquire) == read->reader->cookie
        && atomic_load_explicit(&lane->epoch, memory_order_acquire) == read->epoch;
}

anostr_t ano_res_bytes(const ano_res_read *read, anores_t h)
{
    if (!read_active(read) || h.slot >= RES_SLOT_MAX)
        return anostr_empty();
    res_pub *pub = atomic_load_explicit(&g_directory[h.slot], memory_order_acquire);
    if (pub == NULL || pub->rid != h.rid || pub->generation != h.gen
        || pub->slot != h.slot || pub->size > UINT32_MAX)
        return anostr_empty();
    return anostr_view(pub->data, pub->size);
}

anores_t ano_res_get(ano_res_lifetime lifetime, const char *logical)
{
    anores_t none = {0};
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready() || !owner_thread())
        return none;
    res_freeze();
    uint64_t rid = res_fnv1a64(logical, len);
    ano_mutex_lock(&g_reg.mtx);
    if (domain_of(lifetime, true) == NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        return none;
    }
    uint32_t slot = row_bind(rid, logical, len);
    if (slot == UINT32_MAX) {
        ano_mutex_unlock(&g_reg.mtx);
        return none;
    }
    res_row *row = row_at(slot);
    if (row->payload.data != NULL) {
        anores_t h = { rid, slot, row->generation };
        ano_mutex_unlock(&g_reg.mtx);
        return h;
    }
    ano_mutex_unlock(&g_reg.mtx);

    ano_fspath cand[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, cand, ANO_RES_MAX_MOUNTS + 2);
    for (int i = 0; i < n; i++) {
        void *staging = NULL;
        size_t size = 0;
        int rc = res_read_all(NULL, cand[i].str, &staging, &size);
        if (rc == -1)
            continue;
        if (rc != 0)
            return none;
        res_place_plan plan = {
            .tag = res_tag_from_path(logical, len), .lifetime = lifetime,
            .role = RES_ROLE_PAYLOAD, .operation = RES_OP_LOAD,
            .destination = RES_DEST_VARIABLE_PAYLOAD,
            .provenance = RES_PROVENANCE_NAMESPACE,
            .alignment = ANO_CACHE_LINE,
        };
        res_owned_block home = {0};
        if (res_owned_alloc(&plan, size, &home) != 0) {
            mi_free(staging);
            return none;
        }
        memcpy(home.data, staging, size + 1);
        res_account_copy(&plan, size + 1);
        mi_free(staging);
        anores_t h = res_registry_adopt(logical, &home, NULL, 0);
        if (h.gen == 0)
            res_owned_free(&home, RES_FREE_RETAIL);
        return h;
    }
    return none;
}

anores_t ano_res_get_engine(const char *logical)
{
    return ano_res_get(ano_res_lifetime_engine(), logical);
}

int ano_res_unload(ano_res_lifetime lifetime, anores_t h)
{
    if (!owner_thread())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_live(h);
    if (row == NULL || row->payload.plan.lifetime.owner != lifetime.owner
        || row->payload.plan.lifetime.generation != lifetime.generation) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    int rc = retire_locked(h.slot, row);
    collect_locked();
    ano_mutex_unlock(&g_reg.mtx);
    return rc;
}

int ano_res_unload_engine(anores_t h)
{
    return ano_res_unload(ano_res_lifetime_engine(), h);
}

int ano_res_release(ano_res_lifetime lifetime, anores_t h, void **data, size_t *size)
{
    if (data == NULL || size == NULL || !owner_thread())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_live(h);
    if (row == NULL || row->payload.plan.lifetime.owner != lifetime.owner
        || row->payload.plan.lifetime.generation != lifetime.generation) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    res_pub *pub = atomic_load_explicit(&g_directory[h.slot], memory_order_acquire);
    uint64_t retire_epoch = atomic_fetch_add_explicit(&g_epoch, 1, memory_order_acq_rel) + 1;
    atomic_store_explicit(&g_directory[h.slot], NULL, memory_order_release);
    if (!reader_safe(retire_epoch, NULL)) {
        atomic_store_explicit(&g_directory[h.slot], pub, memory_order_release);
        g_reg.stats.stalled_readers++;
        ano_mutex_unlock(&g_reg.mtx);
        return -2;
    }
    res_domain *d = domain_of(row->payload.plan.lifetime, false);
    if (d && d->live_rows > 0) d->live_rows--;
    if ((row->payload.site.flags & RES_SITE_TRANSFERABLE)
        && row->payload.site.backing != RES_BACK_MULTIPOOL) {
        *data = row->payload.data;
        *size = row->payload.size;
        g_reg.stats.live_bytes -= row->payload.site.serving;
        g_reg.stats.live_blocks--;
        g_reg.stats.transfers++;
        g_reg.stats.transfer_bytes += row->payload.size;
        row->payload = (res_owned_block){0};
    } else {
        void *out = mi_malloc_aligned(row->payload.size + 1, ANO_CACHE_LINE);
        if (out == NULL) {
            atomic_store_explicit(&g_directory[h.slot], pub, memory_order_release);
            if (d) d->live_rows++;
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
        memcpy(out, row->payload.data, row->payload.size + 1);
        *data = out;
        *size = row->payload.size;
        g_reg.stats.copies++;
        g_reg.stats.bytes_copied += row->payload.size + 1;
        g_reg.stats.duplications++;
        g_reg.stats.transfers++;
        g_reg.stats.transfer_bytes += row->payload.size;
        block_free_locked(&row->payload);
    }
    if (row->generation < UINT32_MAX)
        row->generation++;
    else
        row->generation_exhausted = true;
    if (row->dependency_storage.data != NULL) { // a released row keeps NO dependency claim:
        block_free_locked(&row->dependency_storage);        // re-adoption must start clean
        row->dependency_count = 0;
    }
    mi_free(pub);
    g_reg.stats.descriptors_live--;
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

int ano_res_release_engine(anores_t h, void **data, size_t *size)
{
    return ano_res_release(ano_res_lifetime_engine(), h, data, size);
}

// ---------------------------------------------------------------------------------------------
// The frozen consume / cross-lifetime / disclosure surface. STUBS.
//
// TODO(W2, M8): ANO_RES_LIFETIME_SHARED_IMMUTABLE opened beside the engine domain (and the
// bound check at ano_res_domain_open moved in lockstep); ano_res_get's HIT PATH compares the
// resident owner to the requested lifetime and REFUSES instead of aliasing -- today it hands
// level B a handle into level A's mi_heap, and ano_res_domain_retire(A) then calls
// mi_heap_destroy on it. This must land before any squad opens a real WORLD_LEVEL domain in
// production.
//
// TODO(W2, M9): ano_res_take / parcel / parcel_free / parcel_zero_copy, the
// outstanding_parcels teardown barrier, ano_res_domain_retire -> -2 while a parcel is in
// flight, and ano_res_derive as the ONE adoption door. ano_res_release and
// res_registry_external_allocation are DELETED in the same commit.

ano_res_lifetime ano_res_lifetime_shared(void)
{
    return (ano_res_lifetime){0};               // TODO(W2, M8)
}

anostr_t ano_res_bytes_typed(const ano_res_read *read, anores_t h, uint32_t tag)
{
    (void)tag;
    (void)read; (void)h;
    return anostr_empty();                      // TODO(W2, M9): compare pub->tag, then serve
}

int ano_res_take(ano_res_lifetime lifetime, anores_t h, ano_res_parcel *out)
{
    (void)lifetime; (void)h; (void)out;
    return -1;                                  // TODO(W2, M9)
}

void ano_res_parcel_free(ano_res_parcel *parcel)
{
    (void)parcel;                               // TODO(W2, M9)
}

bool ano_res_parcel_zero_copy(const ano_res_parcel *parcel)
{
    (void)parcel;
    return false;                               // TODO(W2, M9): B.6's public oracle
}

anores_t ano_res_derive(ano_res_lifetime lifetime, const ano_res_read *read,
                        anores_t src, uint32_t tag)
{
    (void)lifetime; (void)read; (void)src; (void)tag;
    return (anores_t){0};                       // TODO(W2, M9)
}

anores_t ano_res_get_ex(ano_res_lifetime lifetime, const char *logical, ano_res_share share)
{
    if (share == ANO_RES_SHARE_REFUSE)
        return ano_res_get(lifetime, logical);
    return (anores_t){0};                       // TODO(W2, M8)
}

int ano_res_promote(ano_res_lifetime from, ano_res_lifetime to, anores_t h, anores_t *out)
{
    (void)from; (void)to; (void)h; (void)out;
    return -1;                                  // TODO(W2, M8): charges the DESTINATION cell
}

int ano_res_duplicate(ano_res_lifetime from, ano_res_lifetime to, anores_t h, anores_t *out)
{
    (void)from; (void)to; (void)h; (void)out;
    return -1;                                  // TODO(W2, M8)
}

size_t ano_res_deps(const ano_res_read *read, anores_t h, ano_res_dep *out, size_t cap)
{
    (void)read; (void)h; (void)out; (void)cap;
    return 0;                                   // TODO(W2, M9): COPY-OUT, never a borrowed ptr
}

int ano_res_prefetch(ano_res_lifetime lifetime, const char *logical, ano_res_share share)
{
    (void)lifetime; (void)logical; (void)share;
    return -1;                                  // TODO(W2, M9)
}

int res_registry_name(const ano_res_read *read, anores_t h, char *out, size_t cap)
{
    if (!read_active(read) || out == NULL || cap == 0)
        return -1;
    anostr_t bytes = ano_res_bytes(read, h);
    if (anostr_len(bytes) == 0)
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_live(h);
    if (row == NULL || (size_t)row->name_len + 1 > cap) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    memcpy(out, row->name, (size_t)row->name_len + 1);
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

anores_t res_registry_find(const ano_res_read *read, const char *logical)
{
    anores_t none = {0};
    size_t len;
    if (!read_active(read) || res_path_validate(logical, &len) != 0)
        return none;
    uint64_t rid = res_fnv1a64(logical, len);
    ano_mutex_lock(&g_reg.mtx);
    uint32_t slot = probe_find(rid);
    res_row *row = slot == UINT32_MAX ? NULL : row_at(slot);
    anores_t h = row && row->payload.data ? (anores_t){ rid, slot, row->generation } : none;
    ano_mutex_unlock(&g_reg.mtx);
    return h;
}

anores_t res_registry_adopt(const char *logical, res_owned_block *block,
                            const res_dependency_meta *deps, size_t dep_count)
{
    anores_t none = {0};
    size_t len;
    if (logical == NULL || block == NULL || block->data == NULL
        || res_path_validate(logical, &len) != 0 || !owner_thread())
        return none;
    uint64_t rid = res_fnv1a64(logical, len);
    ano_mutex_lock(&g_reg.mtx);
    if (domain_of(block->plan.lifetime, true) == NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        return none;
    }
    uint32_t slot = row_bind(rid, logical, len);
    if (slot == UINT32_MAX) {
        ano_mutex_unlock(&g_reg.mtx);
        return none;
    }
    res_row *row = row_at(slot);
    if (row->payload.data != NULL) {
        anores_t existing = { rid, slot, row->generation };
        ano_mutex_unlock(&g_reg.mtx);
        return existing;
    }
    if (dep_count > 0) {
        ano_mutex_unlock(&g_reg.mtx);
        res_owned_block dep_block = {0};
        if (root_block_alloc(RES_ROLE_DEPENDENCY, dep_count * sizeof *deps, &dep_block) != 0)
            return none;
        memcpy(dep_block.data, deps, dep_count * sizeof *deps);
        res_account_copy(&dep_block.plan, dep_count * sizeof *deps);
        ano_mutex_lock(&g_reg.mtx);
        row = row_at(slot);
        row->dependency_storage = dep_block;
        row->dependency_count = dep_count;
    }
    row->payload = *block;
    if (publish_locked(slot, row) != 0) {
        row->payload = (res_owned_block){0};
        if (row->dependency_storage.data) block_free_locked(&row->dependency_storage);
        row->dependency_count = 0;
        ano_mutex_unlock(&g_reg.mtx);
        return none;
    }
    *block = (res_owned_block){0};
    anores_t h = { rid, slot, row->generation };
    ano_mutex_unlock(&g_reg.mtx);
    return h;
}

ano_res_allocator_stats ano_res_stats(void)
{
    ano_res_allocator_stats s = {0};
    if (!reg_alive())
        return s;
    ano_mutex_lock(&g_reg.mtx);
    s = g_reg.stats;
    // Chunk footprint comes from the placement roots now: under scoped-pool that is one
    // SMALL multipool per un-winked domain, exactly the per-domain sum this loop always
    // produced. Dead roots report zeros. Heap arenas stay invisible until D19 lands (M6).
    for (uint32_t i = 0; i < RES_ROOT_MAX; i++) {
        ano_mem_stats p = res_place_arena_stats(i, RES_ARENA_SMALL);
        s.chunk_bytes += p.chunk_bytes;
        s.chunk_count += p.chunk_count;
    }
    ano_mutex_unlock(&g_reg.mtx);
    return s;
}

// Per-domain accounting. TODO(W1+W2, M6): the domain's roots, its parent ledger, and its
// residual footprint after a cycle -- B.5/B.7's decisive metric.
int ano_res_domain_stats(ano_res_lifetime lifetime, ano_res_allocator_stats *out)
{
    (void)lifetime; (void)out;
    return -1;                                  // TODO(W1+W2, M6)
}

// Field-wise subtraction. Cumulative counters subtract cleanly; live/peak gauges are
// reported as the AFTER value, because a delta of a gauge is meaningless.
ano_res_allocator_stats ano_res_stats_delta(const ano_res_allocator_stats *before,
                                            const ano_res_allocator_stats *after)
{
    ano_res_allocator_stats d = {0};
    if (before == NULL || after == NULL)
        return d;
    // Through object representation, not pointer arithmetic: struct members are not an
    // array, and indexing past the first is UB even when every field is a size_t.
    static_assert(sizeof d % sizeof(size_t) == 0, "the stats struct is whole size_t fields");
    size_t b[sizeof d / sizeof(size_t)], a[sizeof d / sizeof(size_t)], o[sizeof d / sizeof(size_t)];
    memcpy(b, before, sizeof d);
    memcpy(a, after, sizeof d);
    for (size_t i = 0; i < sizeof d / sizeof(size_t); i++)
        o[i] = a[i] >= b[i] ? a[i] - b[i] : 0;
    memcpy(&d, o, sizeof d);
    d.live_bytes       = after->live_bytes;
    d.live_blocks      = after->live_blocks;
    d.peak_bytes       = after->peak_bytes;
    d.peak_blocks      = after->peak_blocks;
    d.chunk_bytes      = after->chunk_bytes;
    d.chunk_count      = after->chunk_count;
    d.retired_pending  = after->retired_pending;
    d.descriptors_live = after->descriptors_live;
    d.domains_live     = after->domains_live;
    d.rows_bound       = after->rows_bound;
    return d;
}

const void *res_test_row_address(uint32_t slot)
{
    return row_at(slot);
}

int res_test_set_generation(anores_t h, uint32_t generation)
{
    if (!owner_thread()) return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_row *row = row_live(h);
    if (row == NULL) { ano_mutex_unlock(&g_reg.mtx); return -1; }
    res_pub *old = atomic_load_explicit(&g_directory[h.slot], memory_order_acquire);
    row->generation = generation;
    if (generation > g_row_gen_high)
        g_row_gen_high = generation;
    if (old) {
        res_pub *fresh = mi_malloc(sizeof *fresh);
        if (fresh == NULL) { ano_mutex_unlock(&g_reg.mtx); return -1; }
        *fresh = *old;
        fresh->generation = generation;
        atomic_store_explicit(&g_directory[h.slot], fresh, memory_order_release);
        mi_free(old);
    }
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

int res_test_set_owner_generation(ano_res_lifetime lifetime, uint32_t generation)
{
    if (!owner_thread()) return -1;
    ano_mutex_lock(&g_reg.mtx);
    res_domain *d = domain_of(lifetime, true);
    if (d == NULL || d->live_rows != 0) { ano_mutex_unlock(&g_reg.mtx); return -1; }
    d->generation = generation;
    g_owner_generations[lifetime.owner - 1] = generation;
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

// Inputs: none. Output: 0 only after every reader-pinned block and backing heap is gone.
// Invariant: called on the init/owner thread; a pending reader makes shutdown retryable.
int res_registry_shutdown(void)
{
    if (!reg_alive())
        return 0;
    if (!owner_thread())
        return -1;
    ano_mutex_lock(&g_reg.mtx);
    // The scan is idempotent (retired rows have no payload), so EVERY attempt re-runs it: a
    // one-shot flag made an OOM-interrupted first pass skip its surviving rows on retry and
    // destroy their backing roots underneath them.
    for (uint32_t i = 0; i < g_reg.row_count; i++) {
        res_row *row = row_at(i);
        if (row->payload.data != NULL && retire_locked(i, row) != 0) {
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
    }
    for (uint32_t i = 1; i < RES_DOMAIN_MAX; i++)
        if (g_reg.domains[i].state == RES_DOMAIN_LIVE)
            g_reg.domains[i].state = RES_DOMAIN_RETIRING;
    collect_locked();
    if (g_reg.retired != NULL) {
        ano_mutex_unlock(&g_reg.mtx);
        return -1;
    }
    for (uint32_t i = 0; i < RES_READER_MAX; i++) {
        res_reader_lane *lane = reader_lane_at(i);
        if (lane != NULL
            && atomic_load_explicit(&lane->cookie, memory_order_acquire) != 0) {
            ano_mutex_unlock(&g_reg.mtx);
            return -1;
        }
    }
    for (uint32_t i = 0; i < RES_READER_MAX; i++)
        atomic_store_explicit(&g_readers[i], NULL, memory_order_release);
    ano_mem_stripe_destroy(g_reader_stripe);
    g_reader_stripe = NULL;
    g_reg.domains[0].state = RES_DOMAIN_FREE;
    // Placement owns every root heap now: shutdown winks the engine root and any
    // straggler, then telemetry and the extension roster reset for the next init.
    res_place_shutdown();
    res_tel_shutdown();
    res_ext_reset();
    atomic_store_explicit(&g_reg.alive, false, memory_order_release);
    ano_mutex_unlock(&g_reg.mtx);
    ano_mutex_destroy(&g_reg.mtx);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Gamesave loading.

typedef struct save_select {
    const char *slot;
    size_t slot_len;
    bool bounded;
    uint64_t upper;
    bool found;
    uint64_t best;
} save_select;

static void save_select_cb(const char *name, void *ctx)
{
    save_select *s = ctx;
    uint64_t seq;
    if (!res_save_name_seq(name, s->slot, s->slot_len, &seq))
        return;
    if (s->bounded && seq >= s->upper)
        return;
    if (!s->found || seq > s->best) {
        s->found = true;
        s->best = seq;
    }
}

// "<slot>.<seq>.anosave.<hex nonce>.tmp": a save generation caught mid-write-protocol.
// The nonce is the protocol's collision breaker and carries no meaning here, so its width
// is not load-bearing; the seq is, and out_seq reports it. That seq is the name's CLAIM --
// the frame inside must echo it, exactly as for a committed generation.
static bool save_tmp_name(const char *name, const char *slot, size_t slot_len, uint64_t *out_seq)
{
    size_t nlen = strlen(name);
    if (nlen >= MAXPATH || nlen < slot_len + 1 + 1 + 8 + 1 + 1 + 4)
        return false;
    if (memcmp(name, slot, slot_len) != 0 || name[slot_len] != '.')
        return false;
    const char *p = name + slot_len + 1;
    if (*p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9'))
        return false;
    uint64_t seq = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        if (seq > (UINT64_MAX - (uint64_t)(*p - '0')) / 10)
            return false;                       // wider than any seq we could have written
        seq = seq * 10 + (uint64_t)(*p - '0');
    }
    if (strncmp(p, ".anosave.", 9) != 0)
        return false;
    p += 9;
    size_t hex = 0;
    for (; (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'); p++)
        hex++;
    if (hex == 0 || strcmp(p, ".tmp") != 0)
        return false;
    *out_seq = seq;
    return true;
}

// One scan yields one temp: the lexicographically smallest name above the caller's cursor.
// Bounded memory by construction -- a slot with a thousand stranded temps costs a thousand
// scans and one name of storage, never a candidate array that can silently fill up.
typedef struct temp_select {
    const char *slot;
    size_t slot_len;
    const char *after;
    bool found;
    uint64_t seq;
    char next[MAXPATH];
} temp_select;

static void temp_select_cb(const char *name, void *ctx)
{
    temp_select *s = ctx;
    uint64_t seq;
    if (!save_tmp_name(name, s->slot, s->slot_len, &seq) || strcmp(name, s->after) <= 0)
        return;
    if (!s->found || strcmp(name, s->next) < 0) {
        memcpy(s->next, name, strlen(name) + 1);
        s->seq = seq;
        s->found = true;
    }
}

static int replace_adopt(const char *logical, res_owned_block *block, anores_t *out)
{
    size_t len;
    if (res_path_validate(logical, &len) != 0)
        return -1;
    uint64_t rid = res_fnv1a64(logical, len);
    ano_mutex_lock(&g_reg.mtx);
    uint32_t slot = row_bind(rid, logical, len);
    if (slot == UINT32_MAX) { ano_mutex_unlock(&g_reg.mtx); return -1; }
    res_row *row = row_at(slot);
    if (row->payload.data != NULL && retire_locked(slot, row) != 0) {
        ano_mutex_unlock(&g_reg.mtx); return -1;
    }
    row->payload = *block;
    if (publish_locked(slot, row) != 0) {
        row->payload = (res_owned_block){0};
        ano_mutex_unlock(&g_reg.mtx); return -1;
    }
    *block = (res_owned_block){0};
    *out = (anores_t){ rid, slot, row->generation };
    collect_locked();
    ano_mutex_unlock(&g_reg.mtx);
    return 0;
}

typedef struct save_frame_view {
    void *bytes;
    uint32_t format_version;
    uint32_t min_reader_version;
    uint64_t seq;
    const uint8_t *payload;
    size_t payload_len;
} save_frame_view;

typedef enum save_probe_result {
    SAVE_PROBE_VALID = 0,
    SAVE_PROBE_MISSING,
    SAVE_PROBE_DAMAGED,
    SAVE_PROBE_IO,
} save_probe_result;

// Read and validate one frame, committed generation or orphan temp alike. want_seq is the
// seq the FILENAME claims: a frame that does not echo it is a rename masquerade and is
// damaged, whatever its checksums say.
static save_probe_result save_probe_file(const char *abs, uint64_t want_seq,
                                         save_frame_view *view)
{
    int rc = res_read_all(NULL, abs, &view->bytes, &view->payload_len);
    if (rc == -1)
        return SAVE_PROBE_MISSING;
    if (rc != 0)
        return SAVE_PROBE_IO;
    size_t frame_len = view->payload_len;
    if (res_save_validate(view->bytes, frame_len, &view->format_version,
                          &view->min_reader_version, &view->seq, &view->payload,
                          &view->payload_len) != 0
        || view->seq != want_seq) {
        mi_free(view->bytes);
        *view = (save_frame_view){0};
        return SAVE_PROBE_DAMAGED;
    }
    return SAVE_PROBE_VALID;
}

static int save_adopt(ano_res_lifetime lifetime, const char *slot,
                      const save_frame_view *view, anores_t *out)
{
    res_place_plan plan = {
        .tag = RES_TAG_SAVE, .lifetime = lifetime,
        .role = RES_ROLE_PAYLOAD, .operation = RES_OP_SAVE_LOAD,
        .destination = RES_DEST_VARIABLE_PAYLOAD,
        .provenance = RES_PROVENANCE_SAVE_FRAME,
        .alignment = ANO_CACHE_LINE,
    };
    res_owned_block home = {0};
    if (res_owned_alloc(&plan, view->payload_len, &home) != 0)
        return -1;
    memcpy(home.data, view->payload, view->payload_len);
    ((uint8_t *)home.data)[view->payload_len] = 0;
    res_account_copy(&plan, view->payload_len);
    char logical[MAXPATH];
    int w = snprintf(logical, sizeof logical, "saves/%s", slot);
    if (w < 0 || w >= (int)sizeof logical || replace_adopt(logical, &home, out) != 0) {
        res_owned_free(&home, RES_FREE_RETAIL);
        return -1;
    }
    return 0;
}

static ano_res_save_status load_normal_generations(ano_res_lifetime lifetime, const char *slot,
                                                   size_t slot_len, uint32_t reader_version,
                                                   const ano_fspath *wroot,
                                                   const ano_fspath *saves,
                                                   ano_res_save_result *out)
{
    bool bounded = false;
    uint64_t upper = 0;
    bool saw_candidate = false, saw_io = false;
    for (;;) {
        save_select select = {
            .slot = slot, .slot_len = slot_len, .bounded = bounded, .upper = upper,
        };
        if (rmos_scan_dir(saves->str, save_select_cb, &select) != 0)
            return saw_candidate ? ANO_RES_SAVE_IO_ERROR : ANO_RES_SAVE_NOT_FOUND;
        if (!select.found)
            break;
        saw_candidate = true;
        bounded = true;
        upper = select.best;

        char rel[MAXPATH];
        int w = snprintf(rel, sizeof rel, "saves/%s.%llu.anosave", slot,
                         (unsigned long long)select.best);
        ano_fspath abs = w > 0 && w < (int)sizeof rel ? res_join(wroot, rel, (size_t)w)
                                                     : (ano_fspath){0};
        if (abs.length == 0)
            continue;
        save_frame_view view = {0};
        save_probe_result probe = save_probe_file(abs.str, select.best, &view);
        if (probe == SAVE_PROBE_IO) {
            saw_io = true;
            continue;
        }
        if (probe != SAVE_PROBE_VALID)
            continue;
        if (view.min_reader_version > reader_version) {
            out->format_version = view.format_version;
            out->min_reader_version = view.min_reader_version;
            out->seq = view.seq;
            mi_free(view.bytes);
            return ANO_RES_SAVE_READER_TOO_OLD;
        }
        if (save_adopt(lifetime, slot, &view, &out->resource) != 0) {
            mi_free(view.bytes);
            return ANO_RES_SAVE_RESOURCE_ERROR;
        }
        out->format_version = view.format_version;
        out->min_reader_version = view.min_reader_version;
        out->seq = view.seq;
        mi_free(view.bytes);
        return ANO_RES_SAVE_OK;
    }
    if (saw_io)
        return ANO_RES_SAVE_IO_ERROR;
    return saw_candidate ? ANO_RES_SAVE_CORRUPT : ANO_RES_SAVE_NOT_FOUND;
}

// Finish, or bury, every save the write protocol left mid-flight for this slot. A temp is
// the protocol's own O_EXCL file: it is fully written and fsynced before the rename, so a
// temp that VALIDATES is a durable generation that merely lost its directory entry -- we
// complete the interrupted protocol (rename + dir fsync) rather than discard it. One that
// does not validate never happened: the crash landed before the fsync, and it is purged so
// it cannot accumulate against the protocol's own eight-nonce attempt loop.
//
// Never over an existing generation: rmos_rename_new reports a taken destination (1) and we
// leave the temp alone, preserving both frames for a human rather than picking a winner.
//
// Runs before the load, unconditionally -- a recovered temp then re-enters the normal
// generation namespace and is ranked by seq like any other, so an interrupted save is tried
// last exactly when it is older, not because it arrived through this door.
static void recover_all_temps(const char *slot, size_t slot_len, const ano_fspath *saves)
{
    char after[MAXPATH] = {0};
    for (;;) {
        temp_select select = { .slot = slot, .slot_len = slot_len, .after = after };
        if (rmos_scan_dir(saves->str, temp_select_cb, &select) != 0 || !select.found)
            return;
        memcpy(after, select.next, strlen(select.next) + 1);  // strictly advancing: no rescan wedge
        ano_fspath abs = res_join(saves, select.next, strlen(select.next));
        if (abs.length == 0)
            continue;
        save_frame_view view = {0};
        save_probe_result probe = save_probe_file(abs.str, select.seq, &view);
        if (probe != SAVE_PROBE_VALID) {
            if (probe == SAVE_PROBE_DAMAGED)
                rmos_unlink(abs.str);           // torn, truncated, or masquerading: it never happened
            continue;
        }
        mi_free(view.bytes);                    // the frame stays on disk; the load re-reads it
        char name[MAXPATH];
        int w = snprintf(name, sizeof name, "%s.%llu.anosave", slot,
                         (unsigned long long)select.seq);
        ano_fspath dst = w > 0 && w < (int)sizeof name
                       ? res_join(saves, name, (size_t)w) : (ano_fspath){0};
        if (dst.length != 0 && rmos_rename_new(abs.str, dst.str) == 0)
            rmos_sync_dir(saves->str);          // the entry, not just the bytes, must be durable
    }
}

ano_res_save_status ano_res_save_load_ex(ano_res_lifetime lifetime, const char *slot,
                                         uint32_t reader_version,
                                         ano_res_save_result *out)
{
    size_t slot_len;
    if (out == NULL || reader_version == 0 || res_segment_validate(slot, &slot_len) != 0
        || !res_ready() || lifetime.kind != ANO_RES_LIFETIME_SAVE_CONFIG || !owner_thread())
        return ANO_RES_SAVE_INVALID_ARGUMENT;
    *out = (ano_res_save_result){0};
    ano_mutex_lock(&g_reg.mtx);
    bool valid_domain = domain_of(lifetime, true) != NULL;
    ano_mutex_unlock(&g_reg.mtx);
    if (!valid_domain)
        return ANO_RES_SAVE_INVALID_ARGUMENT;
    res_freeze();
    ano_fspath wroot = res_write_root();
    ano_fspath saves = res_join(&wroot, "saves", 5);
    if (saves.length == 0)
        return ANO_RES_SAVE_RESOURCE_ERROR;
    res_save_guard guard = {0};
    if (res_save_begin(slot, slot_len, &guard) != 0)
        return ANO_RES_SAVE_RESOURCE_ERROR;
    recover_all_temps(slot, slot_len, &saves);  // settle mid-flight writes before reading the slot
    ano_res_save_status status = load_normal_generations(lifetime, slot, slot_len,
                                                         reader_version, &wroot, &saves, out);
    res_save_end(&guard);
    return status;
}

anores_t ano_res_save_load(ano_res_lifetime lifetime, const char *slot,
                           uint32_t *format_version, uint64_t *seq)
{
    ano_res_save_result result = {0};
    if (ano_res_save_load_ex(lifetime, slot, UINT32_MAX, &result) != ANO_RES_SAVE_OK)
        return (anores_t){0};
    if (format_version) *format_version = result.format_version;
    if (seq) *seq = result.seq;
    return result.resource;
}
