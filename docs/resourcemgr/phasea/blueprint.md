# ANOPTIC RESOURCE MANAGER — PHASE A FINAL BLUEPRINT

Chief Architect ruling. This document is the contract. Where it contradicts any of the three submissions or any judge note, this document wins.

---

## 0. VERDICT

Spine: **SQUAD SOA-PLANES**. Two of three judges (correctness lens, performance lens) picked it; the third (modularity lens) beat it only on a defect that is fixable without touching the spine. Its load-bearing wins are structural and cannot be grafted into either rival:

- Its Model B is the only one that is genuinely kind-major. CAPABILITY and MINIMAL-CORE both allocate the registry shard in *chunks of mixed-kind bindings*, so under Model B a 32-row chunk lands in one arbitrary kind's metadata arena while holding rows of many kinds. That is B.4's forbidden "a tag in the Model A pool" wearing a kind-major hat. Neither rival noticed.
- It is the only submission that found a live cache pathology in the tree (`g_readers[64]` is 64 × 16-byte lanes, four per cache line, raced by 1.2M reader observations), which is what gives `ano_mem_stripe` a real Stage A production consumer and satisfies A.2's allocator-roster rule that CAPABILITY openly violates.
- Its `res_plane_layout` / `ano_mem_stripe_planes` primitive is literally what C.3 demands. It pre-positions Stage C instead of promising to invent it.
- Its answer to A.4 bullet 7 contains no counting at all.

Grafted, and every graft is load-bearing:

| Graft | From | Why the spine cannot ship without it |
|---|---|---|
| `res_ext` extension registry + fourcc kinds + dense interning | MINIMAL-CORE | Deletes the `res_kind` enum and `kind_from_path`'s switch — the guaranteed four-way merge collision named in *every* area of the gap ledger. This is also the fix for the one charge on which SOA-PLANES lost judge 3. |
| `res_rid_derived(src_rid, tag)` / `res_rid_duplicate(src_rid, owner)` | MINIMAL-CORE | Makes derived-key type confusion unrepresentable *with zero namespace narrowing*. Retracts SOA-PLANES's self-declared wound (banning `#` and `@`) entirely. |
| `ano_res_derive(lifetime, read, src, tag)` as the ONE adoption door | MINIMAL-CORE | One `validate()` site, one dependency-disclosure site, one accounting site, one place a hostile block can enter. This is what makes the concentrated `res_block_open` gate defensible rather than merely centralised. |
| `ano_res_parcel` + `ano_res_take` + `ano_res_parcel_free` + `ano_res_parcel_zero_copy` | MINIMAL-CORE | The transferred block carries its own home. Puts B.6's zero-copy oracle in the public API. Replaces `ano_res_transfer_free(void*)`, which needs a global side-table lookup on every return. |
| `outstanding_parcels` retire barrier (`ano_res_domain_retire` → `-2`) | MINIMAL-CORE | Turns SOA-PLANES's "loud accounted leak" into a refusal the caller must handle. |
| Per-domain `transfer_root` heap that outlives `root` | CAPABILITY | Keeps transfers attributable to a *domain*, which the residual-footprint metric requires. A process-wide transfer heap makes them unattributable. |
| `res_free_mode {RETAIL, WINK}` passed into `placement->free()`; `backing.root` may be NULL | CAPABILITY | Puts the teardown shape in the MODEL, not in a core `if`. Required to express Model E (winkable lifetime root + non-winkable transfer heap) in one core retire order. |
| Debug post-wink assertion: domain live_bytes == 0 | MINIMAL-CORE | A model that lies about winkability fails loudly instead of double-freeing quietly. |
| `res_disposition_allowed` becomes a PURE predicate; promotion bytes charge the DESTINATION cell | CAPABILITY | Today the counters count *questions*. B.5/B.7 judge C and E on promotion cost; a promotion billed to the source is fiction. |
| AoS telemetry counter row, alloc-hot fields in line 0 | MINIMAL-CORE | SOA-PLANES applied its own SoA thesis to a per-event scatter-update — the one access pattern where SoA is wrong — and the cost lands on the alloc path B.8 benchmarks first. |
| Migration order: renderer retains handles BEFORE the vertex layout widens | MINIMAL-CORE | The recon risk register warns about this by name. SOA-PLANES had it backwards. |
| `res_cap_from_plan`-style temporary adapter | CAPABILITY (technique only) | Keeps the tree green through the seam swap with zero test churn in that commit. |

Rejected outright:

- **CAPABILITY's `res_cap` token.** Refuted by its own signatures: `res_cap_req` has an `alignment` field and no `size`, while multipool alignment is `min(class_stride, 4096)` — a function of size. The mint-time refusal it exists to provide is unsound, and every one of its own five models does the check late in `plan()` anyway. What remains is a 16-byte value threaded through every verb, plus an authority lookup and an intern hash on every allocation — a fixed tax that *compresses the spread between the five models* on the very microbenchmark B.8 lists first. Its `uint16_t axes` bitfield also hard-caps roles at 8 against seven already defined. We keep CAPABILITY's *semantics* (refusal, destination-cell charging, purity) and discard the object.
- **`plan_hook`.** All three judges. A review-enforced escape hatch in the routing function is precisely the private shortcut B.1 forbids.
- **MINIMAL-CORE's 3-axis attribution cube.** Its author concedes "promotion of audio PCM specifically can still hide." B.2's four-axis rule exists so a wound cannot hide. Non-negotiable.
- **CAPABILITY's shared-immutable alias edges** ("a refcount wearing a nicer hat", their words). We take SOA-PLANES's no-count answer.
- **`needs_teardown_records`.** Dissolved — see D11 below.

Recorded for the owner: four architectures were commissioned; three were delivered. This blueprint synthesises the three that exist.

---

## 1. EVERY JUDGE DISAGREEMENT, RESOLVED

**D1 — Winner.** J1 (correctness) SOA-PLANES 8.5; J2 (performance) SOA-PLANES 8.4; J3 (modularity) MINIMAL-CORE 8.6. Ruling: SOA-PLANES spine. Correctness and measurability are gates; modularity is a defect class that grafts fix. The reverse is not true — you cannot graft `res_ext` into SOA-PLANES *without* dissolving the kind axis out of its model literals, so we do exactly that (D2), and you cannot graft a five-axis cube into MINIMAL-CORE without redesigning its cube.

**D2 — Is the kind axis inside the placement table fatal?** J3 says yes (adding audio edits `res_kind`, `RES_KIND_COUNT`, `kind_from_path`, every model's `kind_route[RES_KIND_COUNT][RES_ROLE_COUNT]`, every per-kind arena roster). J1/J2 say it is a scheduling constraint. Ruling: J3 is right that it is a defect; J1/J2 are right that it is not fatal. Fix, and it is total: the `res_kind` enum is deleted; kinds are fourcc tags interned to dense ids by `res_ext`; Model B's per-kind roster becomes a **sparse `res_kind_tune[]` keyed by fourcc**, with a default roster for every unlisted kind. Adding audio registers one extension and edits **zero model literals, zero routing tables, and no core file**. The modularity charge is retired.

**D3 — Telemetry storage.** J1 wants SOA-PLANES's five-axis key; J2 says its SoA counter planes scatter one allocation across three cache lines; J3 wants the interned cell. Ruling: five-axis 19-bit packed key, interned once inside `res_place_plan`, cached in `res_site.cell` — with **AoS 128-byte cells whose entire allocation-hot field set lives in line 0**. Full attribution *and* one cache line per allocation. Neither submission achieved both.

**D4 — Attribution axes.** Five: kind, lifetime, role, operation, destination. MINIMAL-CORE's three-axis cube is a gate failure (J1 and J2 concur). Not negotiable.

**D5 — Cell table capacity.** CAPABILITY's 256 cells with a silent overflow bucket is the same order as its realised load (J2). Ruling: 1024 cells; overflow lands in cell 0, is **counted** in `tel_overflow_hits`, fires a Debug assert, and the realised cell population is a recorded Phase A evidence artifact. No contest number may be quoted from a run whose `tel_overflow_hits != 0`.

**D6 — Transfer vs wink-out.** SOA-PLANES: process-lived transfer heap, leak is loud. CAPABILITY: per-domain `transfer_root` outliving `root`. MINIMAL-CORE: parcel + `outstanding_parcels` barrier + retryable retire. Ruling: **all three mechanisms**. `RES_ARENA_TRANSFER` is a per-domain `mi_heap_t` that is never `RES_SITE_WINKABLE`; `ano_res_take` bumps `outstanding_parcels`; `ano_res_domain_retire` returns `-2` (the same retryable shape `ano_res_shutdown` already uses for stalled readers) while any parcel is in flight; `ano_res_shutdown` logs one ERROR naming every outstanding parcel. `outstanding_parcels` is a **teardown barrier on a domain, not a refcount on a resource** — it counts parcels in flight, never handle reads, and it never frees anything when it reaches zero.

**D7 — `ano_res_parcel._site[4]`.** J3's find: 16 opaque bytes cannot hold a `res_site` (allocator + serving + alignment is already 24). Ruling: **deleted**. The `uint64_t token` indexes an owner-side parcel table the design needs anyway for the barrier and the shutdown report. No internal placement layout leaks into the public header — which is what makes A.4 bullet 2's neutral-interface requirement verifiable by diffing `include/anoptic_resources.h`.

**D8 — Reserved path bytes.** SOA-PLANES bans `#` and `@`. MINIMAL-CORE bans `#` and then reopens the hole by minting `"<logical>@<owner>"` duplicate keys with `@` still legal (J1's find). Ruling: **reserve nothing**. Derived and duplicate identities have no string key at all — `res_rid_derived(src_rid, kind_tag)` and `res_rid_duplicate(src_rid, owner_index)` are seeded FNV-1a-64 over a different basis than `res_rid_file`. A `RES_IDENT_DERIVED` ident cannot be resolved from the filesystem; typed views refuse a kind mismatch before dereferencing a byte. Display names (`"models/x.gltf#gfx"`) live in the domain shard as diagnostic text and are never hashed. Type confusion is unrepresentable and the public path alphabet is untouched. This is strictly better than all three submissions.

**D9 — `plan_hook`.** Deleted. Unanimous. If a model cannot be phrased by (root_axis, arena roster, route plane, kind_tune, small_max), the table is under-specified — extend the table for everyone, in the open, and re-run every model.

**D10 — Teardown shape.** SOA-PLANES branches in the core on `res_place_domain_winkable()`; CAPABILITY passes `res_free_mode` into the model. Ruling: CAPABILITY. The core's retire order becomes invariant and Model E's mixed arenas (winkable root + non-winkable transfer heap) become expressible. Plus MINIMAL-CORE's Debug post-wink assertion.

**D11 — `needs_teardown_records` and its fairness tax.** SOA-PLANES charges non-winkable models (A/B/D) a per-domain teardown list and concedes in its own wounds that this "tilts the metadata column toward C/E." Ruling: **the flag and the tax are deleted.** The registry already needs a per-domain list of bound slots to invalidate publication at retire. That list *is* the teardown record; it is allocated from the owning domain's `RES_ARENA_METADATA` for **every** model, and retire walks it calling `res_place_free(site, RES_FREE_WINK)` — a no-op for winkable sites, a real free for the rest. One code path, five models, no asymmetric bookkeeping charge. J1's wound #2 dissolves.

**D12 — Model B's whole-kind teardown is a use-after-free in all three submissions.** `mi_heap_destroy(kind_root)` without invalidating publication and without crossing the epoch grace barrier reads freed memory under any reader in an open scope. Ruling: `res_place_b_kind_wink()` must run the identical invalidate → `queue_retire_locked` → `reader_safe` → `collect_locked` sequence as `ano_res_domain_retire`, and it is a **contest-harness-only** symbol (CAPABILITY's framing, adopted verbatim). Production never calls it.

**D13 — The chunk pool.** SOA-PLANES contradicts itself: Model A's roster has no CHUNK arena, but `resources_stream.c` hardcodes `ano_mem_pool(512 KiB, max_blocks 8)`. Ruling in favour of MINIMAL-CORE: the chunk is **contested**. `resources_stream.c` asks the active placement for a chunk (`role = RES_ROLE_STAGING, destination = RES_DEST_CHUNK`); D/E route it to a bounded `ano_mem_pool`, A routes it to its one multipool, and B.6's "bounded chunk reuse" home ground becomes a real contest rather than a shared primitive every model inherits.

**D14 — Migration order.** Renderer retains handles (delete `ModelAsset`, `parseGltf` → `ano_render_bind_scene`) lands **before** the vertex layout widens, so the renderer is already on views and only one file fights.

**D15 — Mint-time alignment refusal.** Unsound (no size in the request). Ruling: alignment refusal lives in `res_place_plan()`, is authoritative, and `res_site.alignment` reports what the arena will actually deliver. This also kills the live bug at `resources_registry.c:160-165` where `plan->alignment` is silently dropped on the pooled path.

**D16 — The capability token.** Rejected (see §0).

**D17 — Dense kind id stability.** J2's find: MINIMAL-CORE's dense ids depend on registration order, and Model B's root axis *is* the dense kind id, so a registration-order change silently retunes Model B. Ruling: `res_ext_freeze()` **sorts the kind table by fourcc**, so the dense id is a deterministic function of the tag *set*, not of call order. A `static_assert` freezes the id↔tag map for the built-in kinds. A dense id never reaches disk, a pack TOC, or a bake header — fourcc only.

**D18 — Registry shard granularity.** Per-BINDING `res_bind` allocation, never a chunk of mixed-kind bindings. This is the decisive discriminator and it is non-negotiable: it is the only thing that makes Model B a real contestant.

**D19 — Per-domain heap page footprint (the gap nobody closed).** `ano_res_stats()` sums only `ano_mem_multipool_stats(d->pool).chunk_bytes`; the domain `mi_heap_t`'s retained pages are invisible, and today every >1 MiB payload is `mi_malloc_aligned` on the *calling thread's default heap*, not in `d->heap` at all. Residual footprint is B.5/B.7's decisive metric. Ruling: (a) every byte comes from the domain root or a parent chain rooted there (M6); (b) an `ano_mem_parent_ledger` counts acquire/release bytes at the parent seam, which captures every chunk any arena takes from mimalloc; (c) direct `RES_BACK_HEAP` blocks are charged at `mi_usable_size()`, capturing mimalloc's rounding; (d) process RSS is sampled at every cycle boundary as an independent cross-check oracle. The one remaining unmeasured quantity is mimalloc's free-page retention inside a live heap, which `mi_heap_destroy` returns — it is declared, it is bounded by the RSS oracle, and it is never quoted as a model's number. **No residual figure may be published before this lands.**

**D20 — `ano_mem_multipool_cfg` has no class list.** J2's find: B.4 requires "a kind's class-specific size histogram may tune multipool classes," and a `{min_block, max_block}` window of geometric power-of-two classes is not a histogram-tuned class set. All three squads proposed tuning Model B through min/max alone. Ruling: add an optional explicit `classes[]` / `class_count` to the cfg. Without it Model B fights the contest on ground it was never given.

**D21 — Directory growth vs read-path indirection.** SOA-PLANES chunks the identity planes (`plane_dir[slot>>12][slot&4095]`) and admits it has not measured the extra indirection on the hottest read; MINIMAL-CORE keeps a flat static 16384-slot array and defers growability. Ruling, and it takes both: the **publication directory `g_directory` stays flat and static at 16384 `_Atomic(res_pub*)` entries (128 KiB)** — the read path keeps its single indexed load, unchanged, and Stage C freezes exactly that. The **owner-side identity planes are chunked** in 4096-slot chunks and grow by appending a chunk — they are never touched by a reader. Both wounds dissolve.

**D22 — Cross-lifetime sharing without refcounts.** `ANO_RES_LIFETIME_SHARED_IMMUTABLE` is process-lived, created by `ano_res_init` beside the engine domain, and retires only at shutdown. Retiring it while any other domain is live is refused with one ERROR. An alias into a domain that cannot die is not a lifetime hazard and needs no count to prove it. CAPABILITY's alias edges are rejected. `alias_hits` is a reported statistic, not a gate.

**D23 — Stale audit.** `docs/resourcemanager-comprehensive-report.md` claims 7 orphan-temp assertions and 1 keybindings memcmp fail. Both are already fixed in the working tree (`resources_registry.c:1327-1360`; `anotest_persistence.c:145` zero-inits its operand). The resource suite passes today. **Nobody re-fixes them.** M0 records the true baseline.

---

## 2. THE FROZEN SEAMS — CONCRETE C

Everything in this section lands in W0 and is frozen before any parallel work begins.

### 2.1 `src/resources/resources_ext.h` — meaning (NEW, module-private)

```c
#define ANO_FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b)<<8) | \
                             ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))
#define RES_KIND_MAX 32u    // dense-id bound. static_asserted against the registered set.
#define RES_EXT_MAX  16u

typedef struct res_ext_kind {
    uint32_t    tag;        // ANO_FOURCC('R','G','F','X'). Stable on disk, in packs, forever.
    const char *name;       // "graphics.scene"
    bool        derived;    // never resolvable from the filesystem
    bool        bakeable;   // validate() is MANDATORY on RES_PROVENANCE_PACK adopt
} res_ext_kind;

typedef struct res_derive_out {
    res_owned_block     block;
    uint32_t            tag;
    char                display[128];        // diagnostic only; NEVER hashed
    res_dependency_meta deps[RES_DEPS_MAX];
    size_t              dep_count;
} res_derive_out;

typedef struct res_ext {
    const char *name;                                    // "graphics"
    const res_ext_kind *kinds; size_t kind_count;

    uint32_t (*classify)(const char *logical, size_t len);          // 0 = not mine
    int      (*derive)  (const ano_res_read *, anores_t src, ano_res_lifetime,
                         uint32_t want_tag, res_derive_out *out);
    int      (*validate)(uint32_t tag, const void *block, size_t size);   // hostile-input gate
    int      (*deps_of) (uint32_t tag, const void *block, size_t size,
                         res_dependency_meta *out, size_t cap, size_t *n);
    res_disposition (*share_policy)(uint32_t tag);
} res_ext;

int      res_ext_register(const res_ext *);   // owner thread, before res_ext_freeze()
void     res_ext_freeze(void);                // SORTS kinds by fourcc -> deterministic dense ids
uint16_t res_kind_of(uint32_t tag);           // dense id; 0 = unknown
uint32_t res_tag_of(uint16_t kind);
bool     res_kind_derived(uint16_t kind);
bool     res_kind_bakeable(uint16_t kind);
```

`res_kind` (the enum) is **deleted**. `kind_from_path`'s switch is **deleted** and replaced by a walk over registered extensions' `classify()`. Core registers one builtin kind, `ANO_FOURCC('B','Y','T','S')`, dense id 0 by construction.

### 2.2 `src/resources/resources_place.h` — memory (NEW, module-private)

```c
#define RES_ROOT_MAX    64u
#define RES_PLANE_GRAIN 64u   // COMPILE-TIME. Deliberately NOT ANO_CACHE_LINE: a baked block's
                              // interior plane offsets are an ABI and may not vary with the
                              // target's cache line (128 on arm64/Apple).

typedef enum res_arena_id {
    RES_ARENA_METADATA = 0,  // identity shard, names, dep records, pubs, retire records, bind records
    RES_ARENA_SMALL,         // variable payloads <= model->small_max
    RES_ARENA_BULK,          // large payloads
    RES_ARENA_CHUNK,         // fixed RMOS_CHUNK_MAX streaming chunks (ano_mem_pool)
    RES_ARENA_PLANE,         // lane-isolated SoA planes (ano_mem_stripe)
    RES_ARENA_STAGING,       // monotonic ingest/decode/compile staging
    RES_ARENA_TRANSFER,      // blocks that may leave the manager; NEVER winkable
    RES_ARENA_COUNT          // also the REFUSE sentinel in route[][]
} res_arena_id;

typedef enum res_backing {
    RES_BACK_NONE = 0, RES_BACK_MULTIPOOL, RES_BACK_POOL,
    RES_BACK_MONOTONIC, RES_BACK_STRIPE, RES_BACK_HEAP
} res_backing;

enum {
    RES_SITE_TRANSFERABLE = 1u << 0,  // may leave the manager: no copy, no interior free
    RES_SITE_DIRECT_LAND  = 1u << 1,  // an IO read or a decoder may write straight into it
    RES_SITE_WINKABLE     = 1u << 2,  // reclaimed en masse by wink(); free(WINK) is accounting-only
};

// The routing decision. PURE OUTPUT of res_place_plan. Allocates nothing, mutates nothing.
typedef struct res_site {
    void    *allocator;   // ano_mem_multipool* / _pool* / _monotonic* / _stripe* / mi_heap_t*
    size_t   serving;     // what the arena WILL charge. Authoritative. serving_size() is DELETED.
    size_t   alignment;   // what the arena WILL deliver. plan() REFUSES rather than under-deliver.
    uint32_t root;        // index into res_placement.root[]
    uint16_t cell;        // telemetry cell, interned ONCE at plan time
    uint8_t  arena;       // res_arena_id
    uint8_t  backing;     // res_backing
    uint8_t  flags;       // RES_SITE_*
    uint8_t  _pad[7];
} res_site;               // 40 bytes

typedef enum res_free_mode { RES_FREE_RETAIL = 0, RES_FREE_WINK = 1 } res_free_mode;

// ---- the model is DATA ----------------------------------------------------------------
typedef enum res_root_axis {
    RES_ROOT_SINGLE = 0,   // one process root                     (A)
    RES_ROOT_KIND,         // one root per dense kind id           (B)
    RES_ROOT_LIFETIME,     // one root per lifetime domain         (C, E)
    RES_ROOT_ROLE,         // one root per role class              (D)
} res_root_axis;

typedef struct res_arena_spec {
    uint8_t       backing;
    uint8_t       confers;        // RES_SITE_* this arena grants
    uint32_t      min_block, max_block;             // multipool (geometric)
    const size_t *classes; size_t class_count;      // multipool (explicit histogram; B.4)
    uint32_t      block_size, max_blocks;           // pool
    uint32_t      lanes, grain;                     // stripe (grain 0 = ANO_THREAD_LINE)
    uint32_t      first_slab;                       // monotonic
} res_arena_spec;

// SPARSE, keyed by FOURCC. Adding a resource class NEVER edits this table.
typedef struct res_kind_tune {
    uint32_t       tag;
    res_arena_spec arena[RES_ARENA_COUNT];          // backing==RES_BACK_NONE -> use the default
    int8_t         route_override[RES_ROLE_COUNT];  // -1 = fall through to model->route
} res_kind_tune;

typedef struct res_model {
    const char    *name;          // "global-pool" | "scoped-pool" | "model-a".."model-e"
    res_root_axis  root_axis;
    size_t         small_max;     // SMALL/BULK split; SIZE_MAX = one class serves all
    res_arena_spec arena[RES_ARENA_COUNT];                    // the default roster
    const res_kind_tune *kind_tune; size_t kind_tune_count;   // B and E only
    uint8_t        route[RES_ROLE_COUNT][RES_DEST_COUNT];     // RES_ARENA_COUNT = REFUSE
} res_model;                                                  // NO plan_hook. Ever.

typedef struct res_root {
    mi_heap_t *heap;
    void      *arena[RES_ARENA_COUNT];
    uint8_t    backing[RES_ARENA_COUNT];
    uint32_t   key;                    // dense kind | lifetime.owner | role class | 0
    size_t     live_bytes, live_blocks;
    bool       live;
} res_root;

// ---- the whole seam: 11 calls -----------------------------------------------------------
int   res_place_init(const char *model_name);   // once, at res_registry_init
const char *res_place_name(void);               // logged at INFO; reported in stats

int   res_place_domain_open (ano_res_lifetime lt);   // new root, or just a new stats key
void  res_place_domain_wink (ano_res_lifetime lt);   // mi_heap_destroy(root), or a no-op
mi_heap_t *res_place_transfer_heap(ano_res_lifetime lt);  // per-domain; OUTLIVES root
void  res_place_transfer_heap_destroy(ano_res_lifetime lt);

int   res_place_plan (const res_place_plan *p, size_t size, res_site *out);   // PURE ROUTE
void *res_place_alloc(const res_site *s, size_t size);
void  res_place_free (const res_site *s, void *p, size_t size, res_free_mode m);

ano_mem_stats res_place_arena_stats(uint32_t root, res_arena_id arena);
size_t        res_place_domain_live_bytes(ano_res_lifetime lt);   // the post-wink assert reads this

// Contest harness only. Production never calls it. It runs the FULL
// invalidate -> queue_retire -> reader_safe -> collect sequence (D12).
void  res_place_b_kind_wink(uint16_t dense_kind);
```

The whole of `res_place_plan`, and it is the entire contest:

```c
int res_place_plan(const res_place_plan *p, size_t size, res_site *out)
{
    const res_model *m = g_place.model;
    uint16_t dk  = res_kind_of(p->tag);
    uint32_t key = m->root_axis == RES_ROOT_KIND     ? (uint32_t)dk
                 : m->root_axis == RES_ROOT_LIFETIME ? p->lifetime.owner
                 : m->root_axis == RES_ROOT_ROLE     ? role_class(p->role)
                 : 0u;                                            // RES_ROOT_SINGLE
    res_root *r = &g_place.root[key];
    if (!r->live) return -1;

    const res_kind_tune *t = kind_tune_for(m, p->tag);            // sparse fourcc lookup, may be NULL
    int8_t a = t ? t->route_override[p->role] : -1;
    if (a < 0) a = (int8_t)m->route[p->role][p->destination];
    if (a == RES_ARENA_SMALL && size > m->small_max) a = RES_ARENA_BULK;
    if (a >= (int8_t)RES_ARENA_COUNT) return -1;                  // REFUSE, loudly

    const res_arena_spec *spec = spec_of(m, t, (res_arena_id)a);
    if (spec->backing == RES_BACK_NONE || r->arena[a] == NULL) return -1;

    size_t want = p->alignment ? p->alignment : ANO_CACHE_LINE;
    size_t give = arena_alignment(spec, size);
    if (give < want) return -1;                                   // never under-deliver silently

    *out = (res_site){
        .allocator = r->arena[a],
        .serving   = arena_serving(spec, size),
        .alignment = give,
        .root      = key,
        .cell      = res_tel_intern(p),
        .arena     = (uint8_t)a,
        .backing   = spec->backing,
        .flags     = spec->confers
                   | ((m->root_axis == RES_ROOT_LIFETIME && a != RES_ARENA_TRANSFER)
                      ? RES_SITE_WINKABLE : 0),
    };
    return 0;
}
```

Thirty lines. Model-dependent only through data. No hook, no `if (model == ...)`, anywhere.

### 2.3 `src/resources/resources_internal.h` — deltas

```c
// DELETED: enum res_kind. DELETED: res_place_plan.transfer_compatible.
// DELETED: res_owned_block.pooled. DELETED: serving_size(). DELETED: root_plan/
// root_block_alloc/root_block_alloc_locked. DELETED: res_registry_external_allocation.

typedef struct res_place_plan {
    uint32_t         tag;          // FOURCC, not a dense id
    ano_res_lifetime lifetime;
    res_role         role;
    res_operation    operation;
    res_destination  destination;  // + RES_DEST_CHUNK, RES_DEST_STRIPE
    res_provenance   provenance;
    size_t           alignment;    // what the CALLER needs. Refused, never silently dropped.
} res_place_plan;

typedef struct res_owned_block {
    void          *data;
    size_t         size;
    res_site       site;           // carries serving, alignment, arena, backing, flags, cell
    res_place_plan plan;
} res_owned_block;

int  res_owned_plan  (const res_place_plan *, size_t, res_site *out);
int  res_owned_alloc (const res_place_plan *, size_t, res_owned_block *out);   // SAME SIGNATURE
int  res_owned_stage (const res_place_plan *, size_t hint, res_owned_block *out);
int  res_owned_commit(res_owned_block *staged, const res_place_plan *home, res_owned_block *out);
int  res_owned_move  (res_owned_block *from, const res_place_plan *to, res_owned_block *out);
void res_owned_free  (res_owned_block *, res_free_mode);

// PURE. Mutates no counters. The real operations charge the destination's cell.
int  res_disposition_allowed(ano_res_lifetime from, ano_res_lifetime to, res_disposition);

// Identity. No string key ever exists for a derived or duplicate resource.
uint64_t res_rid_file     (const char *logical, size_t len);            // FNV-1a-64, basis A
uint64_t res_rid_file2    (const char *logical, size_t len);            // FNV-1a-64, basis B
uint64_t res_rid_derived  (uint64_t src_rid, uint32_t kind_tag);
uint64_t res_rid_duplicate(uint64_t src_rid, uint32_t owner_index);

// The two-pass source walk. Loose-shadows-pack is an invariant of the WALK.
typedef enum res_source_kind { RES_SRC_DIR = 1, RES_SRC_PACK } res_source_kind;
typedef struct res_source {
    res_source_kind kind; ano_fspath path; const res_pack *pack; uint32_t entry;
} res_source;
int res_candidates_ex(const char *logical, size_t len, res_source *out, int cap);
int res_source_read_sink(const res_source *, const res_sink *, size_t *out_size);

// Destination-aware read. res_read_all becomes a wrapper. EOF-truth preserved verbatim.
typedef struct res_sink {
    void *ctx;
    void *(*reserve)(void *ctx, size_t hint, size_t *out_cap);  // may hand back the HOME block
    void *(*grow)   (void *ctx, size_t need, size_t *out_cap);  // the hint LIED: charged spill
    void  (*commit) (void *ctx, size_t final);
} res_sink;
int res_read_sink(const res_sink *, const char *abs, size_t *out_size);

#define RES_RANGE_EOF (-3)
int   res_read_range  (const res_source *, uint64_t off, size_t len, void *dst);
int   res_hash_file   (const res_source *, uint64_t *hash, uint64_t *size);
void *res_chunk_acquire(ano_res_lifetime);   // routed THROUGH placement (D13)
void  res_chunk_release(ano_res_lifetime, void *);

typedef enum {
    RES_FAULT_AFTER_OPEN_EXCL = 1,   // NEW: the state that produces orphan temps
    RES_FAULT_AFTER_WRITE,
    RES_FAULT_AFTER_SYNC,
    RES_FAULT_AFTER_CLOSE,
    RES_FAULT_AFTER_RENAME,
    RES_FAULT_AFTER_DIR_SYNC,        // NEW
} res_fault_step;
```

### 2.4 `src/resources/resources_block.h` — one framing for every domain (NEW)

```c
#define RES_BLOCK_PLANES_MAX 32u

typedef struct res_block_hdr {
    uint32_t magic;        // == the kind's FOURCC
    uint32_t version;
    uint64_t layout_id;    // compile-time FNV over sizeof/alignof of every struct in the block
    uint64_t block_hash;   // FNV-1a-64 over the whole block with this field zeroed
    uint32_t plane_count;
    uint32_t _pad;
    uint64_t off[RES_BLOCK_PLANES_MAX];   // byte offsets from base, RES_PLANE_GRAIN-aligned
    uint64_t len[RES_BLOCK_PLANES_MAX];   // element counts
} res_block_hdr;

// PURE. A deterministic function of its arguments ONLY. The bake, the pack, and the
// hostile validator all depend on exactly this function.
size_t res_plane_layout(size_t hdr_bytes, const size_t *count, const size_t *elem_size,
                        size_t n_planes, uint64_t *out_off);

int res_block_seal(void *base, size_t size, uint32_t magic, uint32_t version,
                   uint64_t layout_id, size_t n_planes,
                   const uint64_t *off, const uint64_t *len);

typedef struct res_block_view {
    const res_block_hdr *hdr;
    const void *plane[RES_BLOCK_PLANES_MAX];
    uint64_t    count[RES_BLOCK_PLANES_MAX];
    size_t      size;
} res_block_view;

// THE ONE hostile-input gate: magic / version / layout_id / block_hash / plane extents /
// grain alignment / count*elem overflow. Extensions add ONE cross-reference pass on top,
// at LOAD/ADOPT time, never per-view.
int res_block_open(const void *bytes, size_t len, uint32_t magic, uint32_t version,
                   uint64_t layout_id, res_block_view *out);
```

Every conditioned resource in the engine is one plane-set block: graphics scene, decoded pixels, GPU binding table, audio PCM (one f32 plane per channel), script bytecode, level, font bake, pack TOC. One framing, one validator, one bake path, one pack entry type, one hostile-input battery. `res_block_open` is the single most fuzz-worthy function in the module and is treated as such.

Aliasing, settled so nobody re-litigates it: the block is allocated storage with no declared type, so typed access through `anoresgfx_vertex *` is legal under C23 6.5p7, and 64-aligned plane bases over a cache-line-aligned block base satisfy alignment. The existing bare casts may stand.

### 2.5 `src/resources/resources_tel.h` — telemetry (NEW)

```c
#define RES_TEL_CELLS 1024u

typedef struct res_tel_cell {
    /* line 0 -- the ALLOCATION HOT PATH touches this line and no other */
    uint32_t key;                 // packed kind:5 | lifetime:3 | role:3 | op:4 | dest:4
    uint32_t _pad0;
    uint64_t allocs, frees;
    uint64_t requested_bytes, serving_bytes;
    uint64_t live_bytes, peak_bytes;
    uint64_t live_blocks;
    /* line 1 -- copy / transfer / promote / release / retire */
    uint64_t copies, bytes_copied;
    uint64_t transfers, transfer_bytes;
    uint64_t promotions, duplications;
    uint64_t releases_zero_copy, releases_copied;
} res_tel_cell;
static_assert(sizeof(res_tel_cell) == 128, "two lines; alloc touches only line 0");
static_assert(offsetof(res_tel_cell, copies) == 64, "alloc-hot fields must fit line 0");

uint16_t res_tel_intern(const res_place_plan *);   // called ONCE, inside res_place_plan
void res_tel_alloc   (uint16_t cell, size_t requested, size_t serving);
void res_tel_free    (uint16_t cell, size_t requested, size_t serving);
void res_tel_copy    (uint16_t cell, size_t bytes);
void res_tel_transfer(uint16_t cell, size_t bytes, bool zero_copy);
void res_tel_promote (uint16_t dst_cell, size_t bytes, bool copied);   // ALWAYS the DESTINATION
size_t res_tel_snapshot(res_tel_cell *out, size_t cap);                // copy-out; readers never
                                                                       // touch the live cells
size_t res_tel_overflow_hits(void);   // != 0 invalidates every number from that run
```

The cell table lives in `RES_ARENA_PLANE` (`ano_mem_stripe`) — its second Stage A production consumer. All charges are plain non-atomic stores at a known index, owner-thread-only by construction, so Stage C's deletion of `g_reg.mtx` does not silently turn telemetry into a data race.

### 2.6 `include/anoptic_resources.h` — public, additive (plus three deletions)

```c
typedef enum ano_res_lifetime_kind {
    ANO_RES_LIFETIME_ENGINE = 1,
    ANO_RES_LIFETIME_WORLD_LEVEL,
    ANO_RES_LIFETIME_STREAMING,
    ANO_RES_LIFETIME_TRANSIENT_IMPORT,
    ANO_RES_LIFETIME_SAVE_CONFIG,
    ANO_RES_LIFETIME_TOOL_IMPORT,
    ANO_RES_LIFETIME_SHARED_IMMUTABLE,   // APPENDED AFTER TOOL_IMPORT. The bound check at
                                         // resources_registry.c:601-602 moves in lockstep.
} ano_res_lifetime_kind;

ano_res_lifetime ano_res_lifetime_shared(void);   // process-lived; retires only at shutdown

// -- consume: three verbs, three contracts, none disguised as another --------------------

// 1. BORROW. Kind-gated: a font handle can no longer be reinterpreted as a scene block.
anostr_t ano_res_bytes(const ano_res_read *, anores_t);                  // unchanged
anostr_t ano_res_bytes_typed(const ano_res_read *, anores_t, uint32_t tag);

// 2. DESTRUCTIVE TRANSFER. The block knows its own home. No internal layout is exposed.
typedef struct ano_res_parcel { void *data; size_t size; uint64_t token; } ano_res_parcel;
int  ano_res_take(ano_res_lifetime, anores_t, ano_res_parcel *out);  // 0 / -1 / -2 reader-stalled
void ano_res_parcel_free(ano_res_parcel *);                          // owner thread
bool ano_res_parcel_zero_copy(const ano_res_parcel *);               // B.6's public oracle

// 3. DERIVE. The ONE door every conditioned artifact enters through.
anores_t ano_res_derive(ano_res_lifetime, const ano_res_read *, anores_t src, uint32_t tag);

// -- cross-lifetime: explicit, charged, never silent, never counted ----------------------
typedef enum ano_res_share {
    ANO_RES_SHARE_REFUSE = 0,   // what plain ano_res_get does
    ANO_RES_SHARE_ALIAS,        // legal ONLY when the resident owner is SHARED_IMMUTABLE
    ANO_RES_SHARE_PROMOTE,
    ANO_RES_SHARE_DUPLICATE,
} ano_res_share;
anores_t ano_res_get_ex(ano_res_lifetime, const char *logical, ano_res_share);
int ano_res_promote  (ano_res_lifetime from, ano_res_lifetime to, anores_t, anores_t *out);
int ano_res_duplicate(ano_res_lifetime from, ano_res_lifetime to, anores_t, anores_t *out);

// -- dependency disclosure and prefetch --------------------------------------------------
typedef struct ano_res_dep { uint64_t rid; uint32_t tag; uint32_t flags; } ano_res_dep;
size_t ano_res_deps(const ano_res_read *, anores_t, ano_res_dep *out, size_t cap);  // COPY-OUT
int    ano_res_prefetch(ano_res_lifetime, const char *logical, ano_res_share);

// -- ranges, packs, reload ---------------------------------------------------------------
int      ano_res_read_range(const char *logical, uint64_t off, size_t len, void *dst);
anores_t ano_res_get_range (ano_res_lifetime, const char *logical, uint64_t off, size_t len);
int      ano_res_mount_pack(const char *prefix, ano_fspath pack_file);
int      ano_res_pack_build(const char *src_dir, const char *out_pack);
int      ano_res_reload_poll(anores_t *changed, int cap);

// -- accounting --------------------------------------------------------------------------
// ano_res_allocator_stats GROWS (additive only; requested_bytes/serving_bytes keep their
// CUMULATIVE meaning because five tests already subtract snapshots of them):
//   live_requested_bytes, parent_acquires, parent_releases, parent_bytes, class_hits,
//   oversize_hits, external_frag_bytes, staging_bytes, staging_peak, hint_mismatch_copies,
//   releases_zero_copy, releases_copied, release_copy_bytes, retire_ns, registry_probes,
//   max_probe, rehashes, ranged_reads, range_bytes, chunk_acquires, chunk_pool_exhaustions,
//   pack_lookups, pack_hits, pack_bytes_stored, pack_bytes_served, codec_decodes,
//   codec_bytes_in, codec_bytes_out, reload_candidates, reload_confirmed,
//   reload_rejected_same_content, parse_count, alias_hits, outstanding_parcels, residual_bytes
ano_res_allocator_stats ano_res_stats(void);
int  ano_res_domain_stats(ano_res_lifetime, ano_res_allocator_stats *out);
ano_res_allocator_stats ano_res_stats_delta(const ano_res_allocator_stats *before,
                                            const ano_res_allocator_stats *after);
size_t ano_res_stats_cells(res_tel_cell_public *out, size_t cap);   // the five-axis cube
const char *ano_res_placement_name(void);

// -- DELETED -----------------------------------------------------------------------------
// ano_res_slurp                     -> gone. Tests migrate to get/bytes in a read scope.
// ano_res_release / _engine         -> replaced by ano_res_take + ano_res_parcel_free.
// ano_res_resolve / _write / subpath / exists
//                                   -> moved to src/resources/resources_toolpath.h,
//                                      a named test/tool-only internal header.
```

`anores_t`, `ano_res_lifetime`, `ano_res_get`, `ano_res_bytes`, `ano_res_unload`, the read-scope grammar and the save API keep their **exact** shape. A.4 bullet 2's neutral-interface requirement is verifiable by diffing this header.

### 2.7 `include/anoptic_memory_pools.h` — additions

```c
// -- stripe: the fourth allocator. Models D and E are INEXPRESSIBLE without it. -----------
typedef struct ano_mem_stripe ano_mem_stripe;
typedef struct ano_mem_stripe_cfg {
    size_t lanes;       // independently-written lanes; 0 = 1
    size_t grain;       // isolation distance; 0 = ANO_THREAD_LINE
    size_t chunk_hint;  // parent acquisition granularity; 0 = 64 KiB
} ano_mem_stripe_cfg;

ano_mem_stripe *ano_mem_stripe_make(ano_mem_parent, const ano_mem_stripe_cfg *);
// Two different lanes NEVER share a grain-sized region. align 0 = the grain. NULL, never UB.
void *ano_mem_stripe_alloc(ano_mem_stripe *, size_t lane, size_t size, size_t align);
// N parallel SoA arrays from ONE parent acquisition, every base on the grain. This is C.3.
int   ano_mem_stripe_planes(ano_mem_stripe *, const size_t *count, const size_t *elem_size,
                            size_t n_planes, void **out_planes);
void  ano_mem_stripe_reset(ano_mem_stripe *);
void  ano_mem_stripe_destroy(ano_mem_stripe *);
ano_mem_stats ano_mem_stripe_stats(const ano_mem_stripe *);

// -- multipool: an explicit class histogram (B.4 requires it; nobody had it) --------------
typedef struct ano_mem_multipool_cfg {
    size_t min_block, max_block;               // geometric, as today
    const size_t *classes; size_t class_count; // OPTIONAL explicit class list; overrides geometric
} ano_mem_multipool_cfg;
typedef struct ano_mem_class_stats {
    size_t stride, hits, live_blocks, free_blocks;
} ano_mem_class_stats;
size_t ano_mem_multipool_class_stats(const ano_mem_multipool *, ano_mem_class_stats *, size_t cap);

// -- stats grow (additive) ---------------------------------------------------------------
typedef struct ano_mem_stats {
    size_t live_bytes, live_blocks, peak_bytes, peak_blocks, chunk_bytes, chunk_count;
    size_t requested_bytes;      // live REQUESTED (so live internal frag = live - requested)
    size_t parent_acquires, parent_releases, parent_bytes;
    size_t oversize_hits;
} ano_mem_stats;

// -- the parent ledger: the ONLY way per-domain heap footprint becomes measurable (D19) ---
typedef struct ano_mem_parent_ledger {
    size_t acquires, releases, bytes_out, bytes_back, live_bytes, peak_bytes;
} ano_mem_parent_ledger;
ano_mem_parent ano_mem_parent_counting(ano_mem_parent inner, ano_mem_parent_ledger *ledger);
```

### 2.8 `src/resources/resources_os.h` — three ops

```c
// *got == 0 is the ONLY EOF signal. 0 < *got < cap is a SHORT READ, not EOF -- 9P and SMB do
// this routinely -- and the CALLER loops. The loop is never folded into the seam, because its
// termination rule differs between "read to EOF" and "deliver exactly len".
//
// WIN64 CAVEAT, and it goes here so nobody assumes thread-safety and ships a data race:
// ReadFile with an OVERLAPPED offset on an ordinary synchronous handle is positional and
// blocking, but per MSDN it ALSO advances the file pointer. One handle is SINGLE-OWNER in
// Stage A. Stage C opens per-worker handles or flips to FILE_FLAG_OVERLAPPED+event without
// re-signaturing.
int rmos_read_at(rmos_file, uint64_t off, void *buf, size_t cap, size_t *got);

typedef enum rmos_advice {
    RMOS_ADVICE_SEQUENTIAL = 1, RMOS_ADVICE_RANDOM, RMOS_ADVICE_WILLNEED
} rmos_advice;
int rmos_advise(rmos_file, uint64_t off, uint64_t len, rmos_advice);  // advisory; never load-bearing

// ADVISORY ONLY, exactly like rmos_size_hint. mtime LIES on 9P/SMB. Hot reload uses it as a
// FILTER and confirms with a content hash.
int rmos_stat_hint(const char *abs, uint64_t *mtime, uint64_t *size);
```

---

## 3. THE FIVE-MODEL PROOF

Every model is a `static const res_model` **data literal** in `src/resources/resources_models.c`. No registry code, no consumer code, no public header change, no `if (model == ...)` outside the 30-line `res_place_plan`. `res_owned_alloc(plan, size, out)` keeps its exact signature. Verified against the tree: `src/resources/world/`, `src/config/`, `src/keybindings/`, `src/text/`, `src/audio/`, `src/script/`, `src/resources/graphics/` and the renderer reference no placement symbol — they are untouched **by construction**. If a placement change ever forces an edit to one of them, A.4 bullet 2 has been violated and this design is wrong.

The mechanism that makes this work is `res_shard_alloc()`: the core's allocator for the per-binding `res_bind` record, its name text, and its dependency array builds a plan carrying `{tag = the binding's own kind, lifetime = the owning domain, role = REGISTRY|NAME|DEPENDENCY}` and routes it through the **same** `res_place_plan`. Registry shards land wherever the model says, with no core knowledge and no per-model core branch. That is what makes B.1's "changing only one payload pointer is not a contestant" satisfiable without touching a consumer.

```c
// ---------------- MODEL A: one complete multipool -----------------------------------------
static const res_model MODEL_A = {
    .name = "model-a",
    .root_axis = RES_ROOT_SINGLE,      // ONE root heap for the entire manager
    .small_max = SIZE_MAX,             // no size split: one multipool serves EVERYTHING
    .arena = {
      [RES_ARENA_SMALL]    = { RES_BACK_MULTIPOOL, .confers = RES_SITE_DIRECT_LAND,
                               .min_block = ANO_CACHE_LINE, .max_block = 1u<<24 },
      [RES_ARENA_TRANSFER] = { RES_BACK_HEAP,
                               .confers = RES_SITE_TRANSFERABLE | RES_SITE_DIRECT_LAND },
    },
    .route = { /* every (role, dest) -> RES_ARENA_SMALL, except role=TRANSFER -> TRANSFER */ },
};
```
A has no monotonic staging arena and no per-kind arena: parse staging churns the one multipool retail, exactly as B.2 demands ("a hidden allocator cannot absorb a model's predicted wound"). `root_axis != RES_ROOT_LIFETIME`, so no site is `RES_SITE_WINKABLE`, so `res_place_free(..., RES_FREE_WINK)` does **real work** and `wink()` is a no-op: retiring a level is a retail walk of the domain's bound-slot list. A can never accidentally look like C. `ano_res_take` of a sub-1 MiB block **copies**, and `releases_copied` says so.

```c
// ---------------- MODEL B: kind-major -----------------------------------------------------
static const res_kind_tune B_TUNE[] = {
  { .tag = ANO_FOURCC('I','E','N','C'),                             // encoded images
    .arena = { [RES_ARENA_METADATA] = { RES_BACK_MULTIPOOL, .min_block=64,   .max_block=4096 },
               [RES_ARENA_SMALL]    = { RES_BACK_MULTIPOOL, .min_block=4096, .max_block=1u<<22 } } },
  { .tag = ANO_FOURCC('S','H','D','R'),                             // shaders
    .arena = { [RES_ARENA_METADATA] = { RES_BACK_MULTIPOOL, .min_block=64,  .max_block=1024 },
               [RES_ARENA_SMALL]    = { RES_BACK_MULTIPOOL, .min_block=256, .max_block=1u<<16 } } },
  { .tag = ANO_FOURCC('A','P','C','M'),                             // audio PCM
    .arena = { [RES_ARENA_PLANE]    = { RES_BACK_STRIPE, .grain = 0 /* ANO_THREAD_LINE */ },
               [RES_ARENA_SMALL]    = { RES_BACK_MULTIPOOL,
                                        .classes = APCM_CLASSES, .class_count = 6 } },   // D20
    .route_override = { [RES_ROLE_PAYLOAD] = RES_ARENA_PLANE, [0 ... ] = -1 } },
  /* unlisted kinds fall through to the default roster. Adding a kind edits NOTHING here. */
};
static const res_model MODEL_B = {
    .name = "model-b", .root_axis = RES_ROOT_KIND,   // a root HEAP per dense kind id
    .small_max = 256u*1024u,
    .kind_tune = B_TUNE, .kind_tune_count = sizeof B_TUNE / sizeof *B_TUNE,
    .arena = { /* the default roster for every untuned kind */ },
    .route = { /* METADATA|NAME|DEPENDENCY -> METADATA; PAYLOAD|DERIVED -> SMALL/BULK; ... */ },
};
```
The kind selects the ROOT. A kind is a real backing domain with its own heap, its own class histogram and — because `res_bind` records are allocated **per binding** with the binding's own tag — its own genuinely kind-major registry shard. Never "a tag in the Model A pool" (B.4). Whole-kind teardown (subsystem restart) is `res_place_b_kind_wink()`, which runs the full grace sequence. Its hostile ground — a level retirement spanning many kinds — costs a retail walk across every kind root, and the five-axis cube shows the cost per kind.

```c
// ---------------- MODEL C: lifetime-major -------------------------------------------------
static const res_model MODEL_C = {
    .name = "model-c", .root_axis = RES_ROOT_LIFETIME,   // a root HEAP per lifetime domain
    .small_max = SIZE_MAX,                               // one multipool serves the full
    .arena = {                                           // distribution inside each lifetime
      [RES_ARENA_SMALL]    = { RES_BACK_MULTIPOOL, .confers = RES_SITE_DIRECT_LAND,
                               .min_block = ANO_CACHE_LINE, .max_block = 1u<<24 },
      [RES_ARENA_TRANSFER] = { RES_BACK_HEAP,
                               .confers = RES_SITE_TRANSFERABLE | RES_SITE_DIRECT_LAND },
    },
    .route = { /* everything -> SMALL; role=TRANSFER -> TRANSFER */ },
};
```
Every site except TRANSFER is `RES_SITE_WINKABLE`, so `res_place_free(..., WINK)` is accounting-only and retire is **one `mi_heap_destroy`, no retail walk**. Registry shard, names, dep records, payloads, control records and derived objects all live in the level's root, so "residual footprint after a cycle" is a real, measurable zero — and it is real **only because M6 moved the >1 MiB class off the calling thread's default heap into the domain root**. C's hostile ground (cross-level sharing, promotion) runs through `ano_res_promote` and SHARED_IMMUTABLE, and every byte of it is charged to the destination cell. `c_plan` never sets `RES_SITE_TRANSFERABLE` on a payload arena, so every sub-threshold take copies — honestly.

```c
// ---------------- MODEL D: role-split -----------------------------------------------------
static const res_model MODEL_D = {
    .name = "model-d", .root_axis = RES_ROOT_ROLE,   // a root HEAP per role class
    .small_max = 256u*1024u,
    .arena = {
      [RES_ARENA_METADATA] = { RES_BACK_MULTIPOOL, .min_block=64,  .max_block=4096 },
      [RES_ARENA_SMALL]    = { RES_BACK_MULTIPOOL, .confers=RES_SITE_DIRECT_LAND,
                               .min_block=256, .max_block=1u<<18 },
      [RES_ARENA_BULK]     = { RES_BACK_HEAP,      .confers=RES_SITE_DIRECT_LAND },
      [RES_ARENA_CHUNK]    = { RES_BACK_POOL,      .block_size=RMOS_CHUNK_MAX, .max_blocks=8 },
      [RES_ARENA_PLANE]    = { RES_BACK_STRIPE,    .confers=RES_SITE_DIRECT_LAND, .grain=0 },
      [RES_ARENA_STAGING]  = { RES_BACK_MONOTONIC, .confers=RES_SITE_DIRECT_LAND,
                               .first_slab=1u<<20 },
      [RES_ARENA_TRANSFER] = { RES_BACK_HEAP,
                               .confers=RES_SITE_TRANSFERABLE | RES_SITE_DIRECT_LAND },
    },
    .route = {
      [RES_ROLE_REGISTRY]  = { [0 ...] = RES_ARENA_METADATA },
      [RES_ROLE_NAME]      = { [0 ...] = RES_ARENA_METADATA },
      [RES_ROLE_DEPENDENCY]= { [0 ...] = RES_ARENA_METADATA },
      [RES_ROLE_STAGING]   = { [RES_DEST_CHUNK] = RES_ARENA_CHUNK,
                               [0 ...]          = RES_ARENA_STAGING },
      [RES_ROLE_TRANSFER]  = { [0 ...] = RES_ARENA_TRANSFER },
      [RES_ROLE_DERIVED]   = { [RES_DEST_STRIPE] = RES_ARENA_PLANE,
                               [RES_DEST_BULK]   = RES_ARENA_PLANE,
                               [0 ...]           = RES_ARENA_SMALL },
      [RES_ROLE_PAYLOAD]   = { [RES_DEST_BULK]   = RES_ARENA_BULK,
                               [0 ...]           = RES_ARENA_SMALL },
    },
};
```
`RES_SITE_TRANSFERABLE` at **any** size — D's zero-copy hand-off rate is 100% across the whole size distribution, its home ground, and `ano_res_parcel_zero_copy()` proves it. A lifetime SPANS every role root, so no site is winkable and retire touches each role root exactly once via `res_site.root` — B.6's "every resource records the role domains it spans, and retire/release handles all of them exactly once," which `res_site` gives us for free. That is D's honest wound (multi-domain teardown, inter-role fragmentation), and it shows up in `retire_ns` and in the per-role axis of the cube.

```c
// ---------------- MODEL E: the full C x D hybrid ------------------------------------------
static const res_kind_tune E_TUNE[] = { /* per-lifetime AND per-kind payload class rosters */ };
static const res_model MODEL_E = {
    .name = "model-e",
    .root_axis = RES_ROOT_LIFETIME,            // C's root axis: one heap per lifetime domain
    .small_max = 256u*1024u,
    .arena     = { /* IDENTICAL roster to D, but instantiated INSIDE each lifetime root */ },
    .kind_tune = E_TUNE, .kind_tune_count = ...,
    .route     = { /* IDENTICAL to D's role split */ },
};
```
E is literally C's root axis plus D's route plane plus a per-kind arena roster. Nothing in the registry knows it exists. Staging winks at ingest end (`ano_mem_monotonic_reset`), the level domain winks at retire (one `mi_heap_destroy`), and the transfer heap is the per-domain `transfer_root` that outlives it. E receives **no credit for components that do not exist**: every arena it names either has a backing or is `RES_BACK_NONE`, and `RES_BACK_NONE` refuses at plan time, loudly.

What this proves, mechanically. The five models differ in exactly four observable ways: which axis keys the root, whether a site is winkable, which arena a (role, destination) pair routes to, and whether a kind tunes its own roster. Everything else — the registry, epoch reclamation, the publication directory, read scopes, the save protocol, every domain extension, the renderer, the mixer, the VM — is byte-identical across all five. `git diff model-a model-e -- src/ include/` touches exactly `src/resources/resources_models.c`.

---

## 4. WORKSTREAMS

Thirteen workstreams. Exclusive file ownership; no two workstreams may edit the same file. Shared files are split in W0 precisely so this holds.

**W0 — SEAM & FREEZE (integrator; blocks everything).** Owns: all the headers in §2; `src/resources/CMakeLists.txt`; root `CMakeLists.txt` (every `add_subdirectory`, `option(ANOPTIC_ZSTD OFF)`, the `anoptic_pack` target, install rules); `tests/CMakeLists.txt` (**every** new target name, `LABELS "resource"` on every resource target, per-sanitizer TIMEOUT scaling) — landed against stub `.c` files that compile and trivially pass. The split of `resources_core.c` into `resources_core.c` (paths, rids, mounts, write protocol) and `resources_read.c` (candidate walk, sinks, source dispatch). Stub dirs `src/audio/`, `src/script/`, `src/resources/{audio,script,text,codec,pack}/`, `external/lz4/`, `tools/anopak/`, `tools/verify/`. **Consequence: no later workstream ever touches a CMakeLists.txt.**

**W1 — PLACEMENT + TELEMETRY.** Owns `src/resources/resources_place.c`, `resources_models.c`, `resources_tel.c`, `tests/anotest_resplace.c`.

**W2 — REGISTRY + IDENTITY + CROSS-LIFETIME.** Owns `src/resources/resources_registry.c` (exclusive), `resources_ident.c`, `resources_ext.c`, `resources_core.c`, `tests/anotest_resownership.c`, `tests/anotest_resgroups.c`.

**W3 — MEMORY TIER.** Owns `include/anoptic_memory_pools.h` (after W0 freeze), `src/memory/pools.c`, `tests/anotest_mempools.c`, `tests/anotest_mempoolbench.c`. Fully independent of the seam landing; can start immediately.

**W4 — OS + STREAM + CODEC + PACK + BUILDER.** Owns `src/resources/resources_os.h`, `resources_posix.c`, `resources_win64.c`, `resources_read.c`, `resources_stream.c`, `codec/`, `pack/`, `external/lz4/`, `external/zstd/`, `tools/anopak/`, `ATTRIBUTIONS.md`, `tests/anotest_resrange.c`, `anotest_rescodec.c`, `anotest_respack.c`.

**W5 — BLOCK FRAMING + BAKE + HOT RELOAD.** Owns `src/resources/resources_block.c`, `resources_reload.c`, `tests/anotest_resbake.c`, `anotest_reshotreload.c`.

**W6 — GRAPHICS.** Owns `src/resources/graphics/res_gfx_ext.c`, `res_gfx_ingest.c`, `res_gfx_block.c`, `res_gfx_image.c`, `res_gfx_binding.c` (`res_graphics.c`, 688 lines heading for 1600, is split in W0), `tests/anotest_resgfx.c`.

**W7 — RENDERER.** Owns `src/render/**`, `src/vulkan_backend/**` (except `text_raster.c`), `include/anoptic_render.h`, `resources/shaders/**`. Deletes `src/render/gltf/scratch_process.c` (not in CMakeLists, does not compile, references an undeclared `state` — do not revive it).

**W8 — AUDIO.** Owns `src/resources/audio/`, `src/audio/`, `assets/audio/`, `tests/anotest_resaudio.c`.

**W9 — SCRIPT.** Owns `src/resources/script/`, `src/script/`, `assets/scripts/`, `tests/anotest_resscript.c`.

**W10 — WORLD + LEVEL + PERSISTENCE + ENGINE.** Owns `src/resources/world/`, `src/config/`, `src/keybindings/`, `src/engine/main.c`, `assets/levels/`, `tests/anotest_reslevel.c`, `anotest_persistence.c`.

**W11 — TEXT/FONT.** Owns `src/resources/text/res_font.c`, `src/text/text.c`, `src/vulkan_backend/text_raster.c`, `tests/anotest_text.c`.

**W12 — VERIFY.** Owns `tests/CMakeLists.txt` (post-W0), `tests/templates/`, `tests/shim/rmos_hostile.c`, `tests/anotest_resremote.c`, `anotest_resfault.c`, `anotest_resdurability.c`, `anotest_resources.c`, `anotest_resbench.c`, `tools/verify/`, `docs/resourcemgr/`, `docs/benchmarks/`. Also owns the deletion sweep (`ano_fs_chdir_gamepath`, `ano_res_slurp`, the resolve/exists quarantine, the importer-exemption policy) — but the `tests/templates/scratch.h` rebase lands as **its own green commit first**, because 15 test binaries call `scratch_anchor_to_exe` and deleting first reddens the logger suite, where the failure looks like a resource bug.

Dependency gates between workstreams:
- W3, W4, W12 are independent of the seam landing → start at day one.
- W1, W2 land the live seam (M1–M7) → then W5, W6, W8, W9, W10, W11 unblock.
- W7 gates on W6's `RES_KIND_GRAPHICS_BINDING` and the retain-handles conversion, and lands the vertex widening **after** it (D14).
- W10 gates on W6 + W8 + W9 (a level whose scripts and audio do nothing is scaffolding wearing a domain's name).

---

## 5. FREEZE LIST

These land in W0, reviewed by every squad lead, and do not change afterwards without an architect ruling.

1. `src/resources/resources_ext.h` — `res_ext`, `res_ext_kind`, fourcc kinds, `res_kind_of`, `RES_KIND_MAX`.
2. `src/resources/resources_place.h` — `res_arena_id`, `res_backing`, `res_site`, `res_free_mode`, `res_root_axis`, `res_arena_spec`, `res_kind_tune`, `res_model`, `res_root`, the 11-call seam, `RES_PLANE_GRAIN`.
3. `src/resources/resources_block.h` — `res_block_hdr`, `res_plane_layout`, `res_block_seal`, `res_block_open`, `res_block_view`.
4. `src/resources/resources_tel.h` — the 19-bit key, the 128-byte AoS cell (line-0 static_assert), the charge calls, the snapshot.
5. `src/resources/resources_internal.h` — final `res_place_plan` (no `transfer_compatible`), final `res_owned_block` (no `pooled`, carries `res_site`), the six `res_owned_*` verbs, the rid functions, `res_source`/`res_candidates_ex`, `res_sink`, the range/hash/chunk primitives, the two new fault steps.
6. `include/anoptic_resources.h` — SHARED_IMMUTABLE, `ano_res_parcel`/`take`/`parcel_free`/`parcel_zero_copy`, `ano_res_derive`, `ano_res_bytes_typed`, `ano_res_get_ex`/`promote`/`duplicate`, `ano_res_deps`/`prefetch`, ranges, pack, reload, the stats surface, and the three deletions.
7. `include/anoptic_memory_pools.h` — `ano_mem_stripe`, the multipool class list, the grown `ano_mem_stats`, `ano_mem_multipool_class_stats`, `ano_mem_parent_counting`.
8. `src/resources/resources_os.h` — `rmos_read_at`, `rmos_advise`, `rmos_stat_hint`, with the Win64 single-owner caveat in the header comment.
9. `src/resources/codec/res_codec.h` — RAW/LZ4/ZSTD/GDEFLATE(reserved byte), `RES_CODEC_CHUNK = 496 KiB`.
10. `src/resources/pack/res_pack.h` + `include/anoptic_res_pack.h` — the frozen 32-byte header and 48-byte TOC.
11. `include/anoptic_res_graphics.h` — the FULL final form, vertex included (tangent/color/uv1/joints/weights), skins/joints/animations/samplers/cameras/lights, `texref.uv_set` + KHR_texture_transform, `image.{bytes_off,bytes_len,mime}`, manager-owned pixels. Freezing this early is what lets W6 and W7 work in parallel without the vertex fight.
12. `include/anoptic_res_audio.h`, `include/anoptic_audio.h`, `include/anoptic_res_script.h`, `include/anoptic_script.h`, `include/anoptic_res_world.h` (the level schema).
13. `include/anoptic_render.h` — `ano_render_bind_scene(ctx, lifetime, read, anores_t scene) -> anores_t`; `anoRenderAssetCount`/`anoRenderAssetPrimitives` take a handle, not a positional `asset_id`.
14. Root `CMakeLists.txt`, every `src/*/CMakeLists.txt`, `tests/CMakeLists.txt` — every target, option, subdir and LABEL, against stubs.
15. `docs/resourcemgr/verification-matrix.md` — the cell skeleton.

---

## 6. GREEN-AT-EVERY-STEP MIGRATION

The tree is green (`build.bat 5`, `build.sh 5/6/7`) at the end of **every** step. Every step that changes an internal signature updates its call sites in the same commit.

**M0 — Baseline evidence. No code.** W12. Run `build.bat 5`, `build.sh 5/6/7`, the 9P floor cell, the engine smoke. Record commit sha + platform + profile + raw log path in `docs/resourcemgr/verification-matrix.md`. macOS recorded UNRUN with exact repro commands, never claimed green. Also in M0: fix the harness defects that would otherwise corrupt every later judgment — `anotest_resgfx.c:132-134`'s `if (failures) return;` (silently skips every per-field ground-truth assertion once any earlier check failed); the `#ifndef _WIN32` temp-litter blind spot (replaced with `rmos_scan_dir`, on the very platform whose ReplaceFileW path is likeliest to strand a temp); `anotest_resbench.c:42-51`'s `res_registry_stats()` shim, which sets `direct_bytes = pools.live_bytes = s.live_bytes` and then asserts one against the other — **no figure from that shim may enter the contest tables**.

**M1 — W0 freeze.** All 15 items. Stubs compile, stub tests pass. Green.

**M2 — `res_ext` + identity.** W2. `res_ext_register` / `res_ext_freeze` (sorted by fourcc) / `res_kind_of`. Delete the `res_kind` enum and `kind_from_path`'s switch. `res_rid_derived` / `res_rid_duplicate`. Graphics registers itself. Green.

**M3 — Telemetry.** W1. `resources_tel.c`: interning, the AoS cells, the charge calls. `res_account_copy`/`res_account_transfer` stop doing `(void)plan;`. New stats fields are **additive** — `requested_bytes`/`serving_bytes` keep their cumulative meaning because `anotest_resownership.c:172-174` and `anotest_resgroups.c:87` already subtract snapshots of them. Green.

**M4 — `ano_mem_stripe` + pool counters.** W3. Stripe lands with `anotest_mempools` coverage (two lanes never share a grain-sized region; plane bases grain-aligned; reset keeps chunks). `g_readers` becomes its first consumer — measurably faster on `anotest_resownership`, and it is a real false-sharing bug fix. The multipool class list, `ano_mem_parent_counting`, the grown stats. Green.

**M5 — The seam, behavior-identical.** W1 + W2. `resources_place.{h,c}` + `resources_models.c` with exactly **two** models: `"global-pool"` and `"scoped-pool"` — restoring the A.4-bullet-1 scaffold names, the `ANO_RES_PLACEMENT` env var and the INFO log line, all of which the working tree deleted prematurely (`git show HEAD:src/resources/resources_internal.h` still has `res_placement_t`; the working tree has no placement identity in code, tests, logs or env at all). `owned_alloc_locked` becomes `res_place_plan` + `res_place_alloc`; `block_free_locked` routes through `res_owned_block.site`; `serving_size()` is deleted. Under `"scoped-pool"` the routing table reproduces today's behavior byte for byte. A temporary `res_plan_from_legacy()` adapter keeps every call site compiling unchanged (CAPABILITY's technique). Green.

**M6 — Retire the adapter. Domain heap ownership.** W1 + W2. Every internal call site takes the new plan; the adapter, `transfer_compatible` and `serving_size` die. **Every** arena, control record (`res_pub`, `res_retired`), bind record, name, dep array and payload — including the >1 MiB class that today is `mi_malloc_aligned` on the *calling thread's default heap* — comes from the domain root or a parent chain rooted there. `transfer_root` splits out. `res_free_mode` lands, plus the Debug post-wink assertion (`res_place_domain_live_bytes(lt) == 0`). `finish_domains_locked`'s double walk (retail-free everything, THEN `multipool_destroy` + `mi_heap_destroy`) disappears. **Only now is `mi_heap_destroy` genuinely a wink-out, and only now is C/E's marquee claim measurable.** `anotest_resownership.c:151` and `anotest_resbench.c` are updated in this commit. Green.

**M7 — Registry split.** W2. `res_ident` (permanent, root-owned, chunked, non-moving, `+rid2`, `+kind`, `+flags`) vs per-binding `res_bind` (name text, dep array, payload, content_hash, source identity) allocated **per binding** through the seam from the OWNING domain's METADATA arena. Collision refusal compares `{rid, rid2, name_len}` and **never touches name text** — the prerequisite that makes the name-in-shard move sound, and without which `row_bind`'s `memcmp(row->name, ...)` reads a retired domain's memory. `res_test_row_address` returns the ident cell, so `anotest_resownership.c:161`'s non-moving assertion still holds. `g_directory` grows to a flat static 16384 (D21). `root_plan`/`root_block_alloc*` are deleted. Green.

**M8 — Cross-lifetime. THE ALIAS BUG.** W2. `ANO_RES_LIFETIME_SHARED_IMMUTABLE` appended after TOOL_IMPORT (bound check moved in lockstep). `ano_res_get`'s hit path compares the resident owner to the requested lifetime and **refuses** instead of aliasing. `ano_res_get_ex` / `promote` / `duplicate` over `res_owned_move`. `res_disposition_allowed` made pure; promotion bytes charge the DESTINATION cell. **This must land before any squad opens a real WORLD_LEVEL domain in production** — otherwise level B's handles point into level A's `mi_heap` and `ano_res_domain_retire(A)` calls `mi_heap_destroy` on them. New oracles: pointer-equality when zero-copy is claimed; exact `copies`/`bytes_copied` when it is not. Green.

**M9 — Consume verbs.** W2. `ano_res_take` / `ano_res_parcel` / `ano_res_parcel_free` / `ano_res_parcel_zero_copy`; the `outstanding_parcels` barrier; `ano_res_domain_retire` → `-2`; the shutdown ERROR naming every outstanding parcel. `ano_res_derive` as the one adoption door. `ano_res_release`/`_engine` and `res_registry_external_allocation` are **deleted**, with their call sites (`ano_GltfParser.c:200`, `anotest_resgfx.c:271/:310`, `anotest_resources.c`) migrated in the same commit. A grep gate proves no `ano_aligned_free` of manager memory remains. `allocations == frees at shutdown` becomes an assertable oracle — it goes red today, because `res_registry_external_allocation` charges and is never reversed. Green.

**M10 — Destination planning.** W2 + W4. `res_sink` + `res_read_sink`; `ano_res_get` reads into home with a charged spill path (`hint_mismatch_copies`) — direct landing is only sound WITH a spill, because `rmos_size_hint` is a hint by deliberate design and a multipool class block cannot grow in place. `res_read_all` becomes a wrapper. `gfx_slurp` and `save_probe_file` migrate. `res_owned_stage`/`commit` land, and `res_graphics.c` stops calling `mi_heap_new` + `ano_mem_monotonic_make` directly (a private scratch heap outside the hierarchy and outside every statistic, which B.2 forbids by name). Green.

**M11 — Block framing.** W5. `res_block_hdr`, `res_plane_layout`, `res_block_open`, `layout_id`, `block_hash`. The graphics scene becomes version 2 with the full cross-reference validation pass hoisted to load time. The **hostile-block battery** lands under ASan: truncated, bad magic, bad version, bad layout_id, hash mismatch, offsets past end, `count*sizeof` overflow, `prim.material` OOB, `node.mesh` OOB, `node.parent` OOB, child span OOB, `children[i]` OOB, `roots[i]` OOB, `indices[i]` OOB. **This is the highest-severity item in the entire ledger**: `ano_resgfx_scene` bounds-checks array EXTENTS and validates NOTHING INSIDE THEM, safe today only because cgltf constructs the indices; a baked block from a pack turns every one of them into an out-of-bounds read primitive handed straight to the renderer. Green.

**M12 — Renderer retains handles.** W6 + W7. `RES_KIND_GRAPHICS_BINDING` (`'GBND'`): a three-plane derived block `{geometry_pool_index | material_index | bindless_index}` parallel to the scene's `prims[]`, adopted under `res_rid_derived(scene_rid, 'GBND')` with a dependency edge back to the scene. `parseGltf(ctx, const char*)` → `ano_render_bind_scene(ctx, lifetime, read, anores_t scene)`. `ModelAsset`/`ModelMesh`/`ModelNode`/`ModelPrimitive` **deleted** (grep-verified: zero free sites today — the whole blueprint tree leaks for the process lifetime, owned by no domain). `scratch_process.c` deleted. `static ModelAsset *g_assets[16]` deleted. Manager-owned decoded pixels (`STBI_MALLOC`/`REALLOC`/`FREE` routed into the staging arena; decode → copy ONCE into the planned home; `res_account_copy` charges that copy honestly). Green + Sponza smoke.

**M13 — Graphics completion.** W6 + W7, ONE commit, renderer sign-off. `anoresgfx_vertex` gains TANGENT/COLOR_0/TEXCOORD_1/JOINTS_0/WEIGHTS_0, forcing `src/vulkan_backend/vertex/vertex.h`, the static_asserts at `ano_GltfParser.c:23-34`, every `VkVertexInputAttributeDescription` and every shader. Plus skins/joints/inverse-bind matrices; animations with STEP/LINEAR/CUBICSPLINE samplers; embedded `data:` AND GLB bufferView images (`res_graphics.c:553` silently drops BOTH today — `models/viking_room.glb` is already staged and renders untextured with a clean log; **the loud failure lands first**); `texref.uv_set` + KHR_texture_transform; the sampler table; cameras; KHR_lights_punctual. New oracles: a handcrafted skinned+animated `.gltf` and a handcrafted GLB (12-byte header + JSON chunk + BIN chunk), written by the test in `anotest_resgfx.c`'s existing self-staging style — the repo has no skinned or animated asset at all. Green.

**M14 — Stream + codec + pack + bake.** W4 + W5. `rmos_read_at`/`advise`/`stat_hint`; `resources_stream.c` (the chunk **routed through placement**); `res_read_range` (0 / `RES_RANGE_EOF` / -1 / -2 — never a silent partial); `res_hash_file`; `external/lz4` (+ the zstd option, OFF); the anopak runtime; the **two-pass** loose-over-pack walk (pass 1 emits every DIR candidate, pass 2 every PACK candidate — loose-shadows-pack becomes an invariant of the WALK, not of registration order, which matters because today's table is write-root > mounts-newest-first > base and a pack mount would SHADOW the loose base, the exact inverse of the requirement); the deterministic builder; `tools/anopak`; install wiring; the bake loader path. `res_gfx_parse_count` must read 0 after loading a baked scene — prose is not evidence. Green.

**M15 — Hot reload.** W5. `republish_locked`: ONE release-store of the new pub over the old, **never NULL in between** — `ano_res_release`'s store-NULL-then-restore would let a reader acquire a sentinel for a fully live resource, breaking old-complete-or-new-complete. Candidate (winning-source identity + `rmos_stat_hint`) → confirm (`res_hash_file` vs `bind->content_hash`; mtime LIES on 9P/SMB and is only a filter) → publish out of place + full validate + republish. The **derived cascade**: without it, reloading `models/x.gltf` leaves the stale conditioned scene published and serving old geometry forever — hot reload that appears to work and is lying. Green.

**M16 — Audio + script.** W8 + W9. RIFF/WAVE ingest → a PLANAR f32 plane-set block, one plane per channel (the mixer walks one plane per channel: the SoA thesis paying rent inside a decoder). The null sink with an FNV byte oracle over every emitted frame. Tokenize/parse/validate/compile → a bytecode plane-set block; a stack VM with a fuel cap (a hostile script MUST terminate) and a host-binding table, checked against an independent reference evaluator. Green.

**M17 — Levels.** W10. The `anoptic.level` schema (jsmn, no new dep) → a conditioned level plane-set block whose `assets` array IS the disclosed dependency set. `ano_reslevel_open/close`. `main.c` opens a WORLD_LEVEL domain; the renderer names NO asset; `render_api.c:143-152`'s three literals and `spawn_scene`'s hardcoded transforms/lights/integer asset ids die. Save v2 → v3 records the level id. The level's `on_load` script parameterizes what actually renders and its ambient audio actually feeds the sink — otherwise those domains are scaffolding wearing a domain's name. ≥20 open/retire cycles with `live_bytes`, `live_blocks`, `chunk_bytes`, `parent_bytes`, `domains_live`, `retired_pending` and `stalled_readers` all returning to the pre-open baseline. Green.

**M18 — Deletion sweep + evidence.** W11 + W12. `ano_text_font_load`/`_lit` deleted; `FT_New_Face` disappears from `src/`; `anotest_text.c` rewritten onto `ano_res_get` + a HELD read scope + `ano_text_font_load_memory` **in the same commit** (FreeType BORROWS the blob, so an early `read_end` presents as a FreeType crash, not a resource error — ASan/TSan mandatory afterwards). `tests/templates/scratch.h` rebased off `ano_fs_chdir_gamepath` **in its own green commit first**, then the decl and all three platform impls deleted. `ano_res_slurp` deleted; resolve/subpath/exists quarantined behind `resources_toolpath.h`. The written importer-exemption policy (`tools/gen_unicode_tables.c` is build-time codegen, not an asset importer; the test oracles' raw `fopen` is precisely what gives them the power to falsify the manager), so the grep gate has a defined allow-list. Bounds saturation + sizing evidence. The full matrix. Green.

**M19 — The five models as data.** W1. `model-a`..`model-e` added to `resources_models.c` behind a test-only selector (B.2 permits exactly this); production still logs the scaffold name per A.4. **Zero edits to the core. Zero edits to any consumer.** Stage A ends here, and Stage B is a bench campaign rather than a rewrite.

---

## 7. PHASE B PREPARATION — WHAT PHASE A MUST LEAVE BEHIND

**7.1 The contest harness seam.** `ANO_RES_PLACEMENT=global|scoped|model-a|…|model-e`, read once at `res_registry_init`, logged at INFO (`resources: placement scaffold '%s'`), reported in `ano_res_stats` and stamped into every benchmark artifact. Production ships `scoped` and logs the truthful scaffold name; the five models are reachable only through the test-only selector. A bench run cannot lie about which strategy produced a number.

**7.2 The model selector scaffold.** Five `static const res_model` literals. `tests/anotest_resplace.c` is the **routing oracle**: for every model × every (tag, lifetime, role, operation, destination, size) it asserts the expected arena, the expected root, the expected `serving`, the expected `alignment`, the expected flags, and that an unroutable plan is REFUSED. Plus the teardown-shape oracle (winkable models free nothing at RETAIL-in-WINK; non-winkable models free everything) and the post-wink `live_bytes == 0` assertion for all five.

**7.3 Preregistered scenarios and corpus.** Fixed before any model is written, so nobody tunes a model to a scenario invented after the fact.

| Scenario | Corpus | The number it decides |
|---|---|---|
| Allocation microstructure | synthetic; sizes 16 B … 64 MiB; alignments 16 … 4096 | per-alloc ns p50/p95/p99/p999, class hits, oversize hits, parent calls |
| Level cycle × 20 | Sponza + viking_room + GlassHurricane + 8 audio clips + 4 scripts + fonts | retire_ns, residual_bytes, parent_bytes, RSS delta, live_bytes back to baseline |
| Cross-lifetime | 2 overlapping WORLD_LEVEL domains + SHARED_IMMUTABLE, 30% asset overlap | promotions, duplications, bytes_copied, zero-copy promotion rate |
| Consume hand-off | full size distribution, 20 B … 64 MiB, GPU + audio sinks | releases_zero_copy / releases_copied, by kind, by size decile |
| Streaming churn | 512 KiB chunks over a 2 GiB synthetic stream | chunk_acquires, chunk_pool_exhaustions, parent calls |
| Pack/bake | cold + warm mount of the installed tree | mount_ns, parse_count (must be 0), TOC bsearch probes, codec bytes |
| Registry probes | 8192 distinct logicals, 40% derived | registry_probes, max_probe, rehashes, lookup ns |
| Subsystem restart | whole-kind teardown (Model B home ground) | retire_ns for one kind — measured through the FULL grace sequence, never on a crash |
| Hostile input | the block battery, the TOC corruption matrix | refusal correctness under ASan/UBSan; not a perf number |

**7.4 Stats every model must report.** The full five-axis cube (`ano_res_stats_cells`), plus per-domain (`ano_res_domain_stats`) and per-arena (`res_place_arena_stats`): requested / serving / live / peak bytes and blocks; `live_requested_bytes` (so live internal fragmentation is `live_bytes - live_requested_bytes`, not a lifetime total); `parent_acquires` / `parent_releases` / `parent_bytes`; `class_hits[]` and `oversize_hits`; `external_frag_bytes`; copies / bytes_copied; promotions / duplications and their bytes and ns, charged to the **destination**; `releases_zero_copy` / `releases_copied` / `release_copy_bytes`; `retire_ns`; `registry_probes` / `max_probe` / `rehashes`; `staging_bytes` / `staging_peak`; `hint_mismatch_copies`; `residual_bytes` after every cycle; RSS delta. `tel_overflow_hits` must be 0 or the run is void.

**7.5 Evidence discipline.** Every bench run writes `docs/benchmarks/<stamp>-<model>-<scenario>.json`: raw per-op samples, the full cube snapshot before and after each phase, plus commit sha, build profile, platform, corpus manifest, run count and the exact repro command. A disabled benchmark plus prose is not evidence. No number from the old `res_registry_stats()` shim may be carried forward.

**7.6 Bounds evidence.** Peak rows / domains / readers / cells across a full level cycle, recorded before Stage C freezes the publication scheme. Growing a static directory of atomic pointers after that point is a redesign, not a constant bump.

---

## 8. RECORDED WOUNDS

Stated here so nobody discovers them in a bug report and nobody sells them as features.

- **The engine gets slower and fatter before it gets better.** Stage A is owner-thread-synchronous by law (A.4 bullet 4), so opening a level serializes glTF ingest + image decode + WAV decode + script compile on the init thread: a multi-second hitch on Sponza. And retaining Sponza's scene block for the level's life pins a multi-MiB block that today is unloaded right after GPU upload. Both are counted and reported. Neither may be "fixed" by sneaking a loader thread in ahead of Stage C: `res_owned_alloc`, `ano_res_get`, `ano_res_domain_open` and `res_registry_adopt` are all owner-thread-gated, and `mi_heap_destroy` is single-thread-owner, so a helpful worker thread corrupts mimalloc. Record the cold-open wall time as the baseline the ticket system will have to beat.
- **One `res_block_open` means one bug is every domain's bug.** Concentrated blast radius, chosen deliberately over six validators that are each "probably fine" for the sharpest hole in the ledger. It is the most fuzz-worthy function in the module and is treated as such.
- **`RES_PLANE_GRAIN` is 64, not `ANO_CACHE_LINE`.** On a 128-byte-line machine (arm64/Apple), plane bases inside a conditioned block are 64-aligned, not cache-line aligned: the bake ABI beats the cache line. The affected platform is exactly the one we cannot run. Said out loud.
- **`res_site` is 40 bytes in every `res_owned_block`**, versus today's single `bool pooled`. On a corpus of thousands of tiny audio clips that is not trivial. We could pack it by recomputing `serving` and `alignment` at free time — but recomputing is exactly the `serving_size()` sin we are deleting, and it is how a retuned model silently diverges from its accounting. We store the truth and name the price.
- **A forgotten parcel is a refusal, not corruption — but it is still an API burden.** `ano_res_domain_retire` returns `-2` while a parcel is in flight, and a caller who does not loop leaks a domain. The alternative — letting `mi_heap_destroy` race an outstanding transfer — is a double-free that only fires when a domain retires with a transfer in flight.
- **mimalloc's in-heap free-page retention is unmeasured.** `parent_bytes` captures every chunk any arena takes; `mi_usable_size` captures rounding on direct blocks; RSS at every cycle boundary is the cross-check. What remains is bounded by the RSS oracle and is never quoted as a model's number.
- **macOS is UNRUN.** Recorded with exact repro commands. Never claimed green.