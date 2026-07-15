/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anopak runtime and deterministic builder. Header/TOC frozen LE. No struct overlay.
// Layout: header[32], TOC[48*n] sorted by rid, toc_hash[8], then per-entry 64-aligned data regions.
// Mount reads header+TOC only. Corrupted anything REFUSES. Fresh handle per read.

#include "res_pack.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_log.h>

#include "../codec/res_codec.h"
#include "../resources_ext.h"

#define PACK_DATA_ALIGN 64u
#define PACK_CHUNK_REC  16u
#define PACK_CHUNK_RAW  0x80000000u
#define PACK_ENTRY_MAX  (1u << 24)              // sanity bound; read truth is the law

/* Little-endian helpers */

// Byte helpers and the folded chunk hash.

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i); return v; }
static uint64_t get64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

static uint32_t fold32(uint64_t h) { return (uint32_t)(h ^ (h >> 32)); }

// Chunk count for raw bytes. raw == 0 -> 0 chunks.
static uint64_t chunk_count_of(uint64_t raw)
{
    return raw == 0 ? 0 : (raw + RES_CODEC_CHUNK - 1) / RES_CODEC_CHUNK;
}

/* Positional exact read */

// Short read loops. Early EOF is corruption.

static int read_exact(rmos_file f, uint64_t off, void *dst, size_t len)
{
    uint8_t *p = dst;
    size_t done = 0;
    while (done < len) {
        size_t want = len - done;
        if (want > RMOS_CHUNK_MAX)
            want = RMOS_CHUNK_MAX;
        size_t got = 0;
        if (rmos_read_at(f, off + done, p + done, want, &got) != 0)
            return -1;
        if (got == 0)
            return -1;                          // the TOC promised these bytes: truncation
        done += got;
    }
    return 0;
}

/* Mount table */

// Owner-written at mount, read-only after.

static res_pack g_packs[ANO_PACK_MAX];
static char     g_prefixes[ANO_PACK_MAX][MAXPATH];
static uint64_t g_toc_hashes[ANO_PACK_MAX];     // the mounted TOC generation, re-checked per read
static int      g_pack_count;

// Inputs: mount prefix, pack path. Output: 0 mounted / -1 refused.
// Validates magic/version/SORTED/hashes/rid order/codec/offsets. TOC lands via placement (engine lifetime).
int res_pack_mount(const char *prefix, ano_fspath file)
{
    if (file.length == 0 || g_pack_count >= ANO_PACK_MAX)
        return -1;
    char canon[MAXPATH];
    size_t clen = 0;
    if (res_prefix_canon(prefix ? prefix : "", canon, &clen) != 0)
        return -1;

    rmos_file f;
    if (rmos_read_open(file.str, &f) != 0) {
        ano_log(ANO_ERROR, "respack: cannot open %s", file.str);
        return -1;
    }

    uint8_t hb[32];
    if (read_exact(f, 0, hb, sizeof hb) != 0) {
        rmos_read_close(f);
        return -1;
    }
    ano_pack_header hdr = {
        .magic       = get32(hb + 0),
        .version     = get16(hb + 4),
        .codec       = hb[6],
        .flags       = hb[7],
        .entry_count = get32(hb + 8),
        .reserved    = get32(hb + 12),
        .toc_off     = get64(hb + 16),
        .header_hash = get64(hb + 24),
    };
    if (hdr.magic != ANO_PACK_MAGIC || hdr.version != ANO_PACK_VERSION
        || hdr.flags != ANO_PACK_FLAG_SORTED    // exactly SORTED: no unknown flag bits
        || hdr.reserved != 0
        || hdr.header_hash != res_fnv1a64(hb, 24)
        || !res_codec_available((res_codec_id)hdr.codec)
        || hdr.entry_count > PACK_ENTRY_MAX
        || hdr.toc_off < sizeof hb) {
        ano_log(ANO_ERROR, "respack: refused header: %s", file.str);
        rmos_read_close(f);
        return -1;
    }
    uint64_t toc_bytes = (uint64_t)hdr.entry_count * 48u;

    // Raw TOC + trailing hash, verified before a single field is believed. A truncated file
    // refuses via read_exact -- no size hint gets a veto over a readable pack.
    uint8_t *raw = NULL;
    if (toc_bytes > 0) {
        raw = mi_malloc((size_t)toc_bytes);
        if (raw == NULL || read_exact(f, hdr.toc_off, raw, (size_t)toc_bytes) != 0) {
            mi_free(raw);
            rmos_read_close(f);
            return -1;
        }
    }
    uint8_t th[8];
    if (read_exact(f, hdr.toc_off + toc_bytes, th, sizeof th) != 0
        || get64(th) != res_fnv1a64(raw, (size_t)toc_bytes)) {
        ano_log(ANO_ERROR, "respack: TOC checksum refused: %s", file.str);
        mi_free(raw);
        rmos_read_close(f);
        return -1;
    }
    rmos_read_close(f);

    // The TOC block is manager-owned metadata in the engine domain.
    res_owned_block blk = {0};
    ano_pack_entry *toc = NULL;
    if (hdr.entry_count > 0) {
        res_place_plan plan = {
            .tag         = RES_TAG_BYTES,
            .lifetime    = ano_res_lifetime_engine(),
            .role        = RES_ROLE_REGISTRY,
            .operation   = RES_OP_ADOPT,
            .destination = RES_DEST_METADATA,
            .provenance  = RES_PROVENANCE_PACK,
            .alignment   = alignof(ano_pack_entry),
        };
        if (res_owned_alloc(&plan, (size_t)hdr.entry_count * sizeof(ano_pack_entry), &blk) != 0) {
            ano_log(ANO_ERROR, "respack: TOC allocation refused (registry live?): %s", file.str);
            mi_free(raw);
            return -1;
        }
        toc = blk.data;
        uint64_t prev_rid = 0;
        for (uint32_t i = 0; i < hdr.entry_count; i++) {
            const uint8_t *p = raw + (size_t)i * 48u;
            ano_pack_entry e = {
                .rid = get64(p), .rid2 = get64(p + 8), .data_off = get64(p + 16),
                .raw_size = get64(p + 24), .stored_size = get64(p + 32),
                .tag = get32(p + 40), .codec = p[44], .flags = p[45],
                .reserved = get16(p + 46),
            };
            uint64_t nchunks = chunk_count_of(e.raw_size);
            uint64_t idx = nchunks * PACK_CHUNK_REC;
            bool ok = (i == 0 || e.rid > prev_rid)                       // strict: SORTED, no dupes
                   && res_codec_available((res_codec_id)e.codec)         // GDEFLATE et al REFUSED here
                   && (e.flags & ~(uint8_t)ANO_PACK_ENTRY_BLOCK) == 0    // no unknown entry flags
                   && e.reserved == 0
                   && e.raw_size <= UINT64_MAX - RES_CODEC_CHUNK + 1     // chunk_count_of is exact
                   && e.raw_size <= UINT64_MAX - idx                     // idx + raw cannot wrap
                   && e.stored_size >= idx + nchunks                     // >= index + >=1 byte/chunk
                   && e.stored_size <= idx + e.raw_size                  // each chunk stored <= raw
                   && (e.data_off >= sizeof hb)
                   && e.stored_size <= UINT64_MAX - e.data_off;          // in-entry offsets wrap-free
            if (e.raw_size == 0)
                ok = ok && e.stored_size == 0;                          // empty entry: no data region
            if (!ok) {
                ano_log(ANO_ERROR, "respack: refused TOC entry %u: %s", i, file.str);
                res_owned_free(&blk, RES_FREE_RETAIL);
                mi_free(raw);
                return -1;
            }
            prev_rid = e.rid;
            toc[i] = e;
        }
    }
    mi_free(raw);

    res_pack *pk = &g_packs[g_pack_count];
    memcpy(g_prefixes[g_pack_count], canon, clen + 1);
    pk->prefix    = clen > 0 ? anostr_view(g_prefixes[g_pack_count], clen) : anostr_empty();
    pk->file      = file;
    pk->hdr       = hdr;
    pk->toc       = toc;
    pk->toc_block = blk;
    g_toc_hashes[g_pack_count] = get64(th);
    g_pack_count++;
    ano_log(ANO_INFO, "respack: mounted %s (%u entries, prefix '%s')",
            file.str, hdr.entry_count, canon);
    return 0;
}

void res_pack_unmount_all(void)
{
    for (int i = 0; i < g_pack_count; i++) {
        if (g_packs[i].toc_block.data != NULL)
            res_owned_free(&g_packs[i].toc_block, RES_FREE_RETAIL);
        g_packs[i] = (res_pack){0};
        g_prefixes[i][0] = '\0';
    }
    g_pack_count = 0;
}

int res_pack_count(void)
{
    return g_pack_count;
}

const res_pack *res_pack_at(int i)
{
    return (i >= 0 && i < g_pack_count) ? &g_packs[i] : NULL;
}

// Inputs: mounted pack and pack-relative logical path. Output: TOC entry index, or -1. rid2 mismatch REFUSED.
int res_pack_find(const res_pack *pack, const char *logical, size_t len)
{
    if (pack == NULL || logical == NULL || len == 0 || pack->toc == NULL)
        return -1;
    uint64_t rid = res_rid_file(logical, len);
    uint32_t lo = 0, hi = pack->hdr.entry_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (pack->toc[mid].rid < rid)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= pack->hdr.entry_count || pack->toc[lo].rid != rid)
        return -1;
    if (pack->toc[lo].rid2 != res_rid_file2(logical, len)) {
        ano_log(ANO_ERROR, "respack: rid2 mismatch for '%.*s' -- corrupt pack refused",
                (int)len, logical);
        return -1;
    }
    return (int)lo;
}

/* Chunk walker */

// Deliver raw bytes [off, off+len). Hash-verify STORED bytes before decode. Decode must match known length.

static int pack_read_chunks(const res_pack *pack, const ano_pack_entry *e,
                            uint64_t off, size_t len, uint8_t *dst)
{
    size_t sbound = res_codec_bound((res_codec_id)e->codec, RES_CODEC_CHUNK);
    if (sbound < RES_CODEC_CHUNK)
        sbound = RES_CODEC_CHUNK;               // RAW-fallback chunks store up to a full chunk
    uint64_t nchunks = chunk_count_of(e->raw_size);
    uint64_t first = off / RES_CODEC_CHUNK;
    uint64_t last  = (off + len - 1) / RES_CODEC_CHUNK;
    if (last >= nchunks)                         // arithmetic guard (off+len already <= raw_size)
        return -1;
    size_t   nidx  = (size_t)(last - first + 1);

    rmos_file f;
    if (rmos_read_open(pack->file.str, &f) != 0)
        return -1;
    // Reads open a FRESH handle, so the file may have been rebuilt or replaced since mount.
    // The TOC generation must still be on disk, or ITS chunk records would be served under
    // OUR table: 8 re-read bytes per operation buy the refusal.
    uint8_t gen[8];
    if (read_exact(f, pack->hdr.toc_off + (uint64_t)pack->hdr.entry_count * 48u,
                   gen, sizeof gen) != 0
        || get64(gen) != g_toc_hashes[pack - g_packs]) {
        ano_log(ANO_ERROR, "respack: pack changed underfoot, refused: %s", pack->file.str);
        rmos_read_close(f);
        return -1;
    }
    int rc = -1;
    uint8_t *idx = mi_malloc(nidx * PACK_CHUNK_REC);
    uint8_t *stored = mi_malloc(sbound);
    uint8_t *edge = NULL;                        // lazily: only partially-covered chunks need it
    if (idx == NULL || stored == NULL)
        goto out;
    if (read_exact(f, e->data_off + first * PACK_CHUNK_REC, idx, nidx * PACK_CHUNK_REC) != 0)
        goto out;

    uint64_t prev_end = 0;
    for (uint64_t i = first; i <= last; i++) {
        const uint8_t *rec = idx + (size_t)(i - first) * PACK_CHUNK_REC;
        uint64_t rel      = get64(rec);
        uint32_t stored_w = get32(rec + 8);
        uint32_t hash32   = get32(rec + 12);
        size_t   snb      = stored_w & ~PACK_CHUNK_RAW;
        bool     is_raw   = (stored_w & PACK_CHUNK_RAW) != 0;
        uint64_t c0       = i * RES_CODEC_CHUNK;
        size_t   raw_i    = e->raw_size - c0 > RES_CODEC_CHUNK
                          ? RES_CODEC_CHUNK : (size_t)(e->raw_size - c0);
        // Index sanity: inside the entry's stored region, after the index, sane size, and
        // in-order without overlap across the window -- the builder's layout is contiguous,
        // and two records naming one stored payload is a malformed pack, refused.
        if (snb == 0 || snb > sbound
            || rel < nchunks * PACK_CHUNK_REC
            || rel > e->stored_size || snb > e->stored_size - rel
            || (i > first && rel < prev_end)
            || (is_raw && snb != raw_i)
            || (!is_raw && snb >= raw_i))        // a compressed chunk is strictly smaller
            goto refuse;
        prev_end = rel + snb;
        if (read_exact(f, e->data_off + rel, stored, snb) != 0)
            goto out;
        if (fold32(res_fnv1a64(stored, snb)) != hash32)
            goto refuse;                        // payload corruption: refuse, never serve

        uint64_t s  = off > c0 ? off : c0;
        uint64_t e2 = off + len < c0 + raw_i ? off + len : c0 + raw_i;
        size_t   need = (size_t)(e2 - s);
        if (need == raw_i) {                    // chunk fully inside the range: land direct
            uint8_t *out = dst + (size_t)(c0 - off);
            if (is_raw)
                memcpy(out, stored, raw_i);
            else if (res_codec_decode((res_codec_id)e->codec, stored, snb, out, raw_i) != raw_i)
                goto refuse;
        } else {                                // edge chunk: decode whole, copy the slice
            if (is_raw) {
                memcpy(dst + (size_t)(s - off), stored + (size_t)(s - c0), need);
            } else {
                if (edge == NULL && (edge = mi_malloc(RES_CODEC_CHUNK)) == NULL)
                    goto out;
                if (res_codec_decode((res_codec_id)e->codec, stored, snb, edge, raw_i) != raw_i)
                    goto refuse;
                memcpy(dst + (size_t)(s - off), edge + (size_t)(s - c0), need);
            }
        }
    }
    rc = 0;
    goto out;
refuse:
    ano_log(ANO_ERROR, "respack: corrupt chunk refused: %s", pack->file.str);
out:
    mi_free(idx);
    mi_free(stored);
    mi_free(edge);
    rmos_read_close(f);
    return rc;
}

// Whole-entry read into a destination-aware sink. 0 / -1.
int res_pack_read_sink(const res_pack *pack, uint32_t entry, const res_sink *sink,
                       size_t *out_size)
{
    if (pack == NULL || sink == NULL || sink->reserve == NULL
        || pack->toc == NULL || entry >= pack->hdr.entry_count)
        return -1;
    const ano_pack_entry *e = &pack->toc[entry];
    if (e->raw_size > SIZE_MAX)
        return -1;
    size_t raw = (size_t)e->raw_size;
    size_t cap = 0;
    void *buf = sink->reserve(sink->ctx, raw, &cap);
    if (raw > 0 && (buf == NULL || cap < raw)) {
        if (sink->grow == NULL)
            return -1;
        buf = sink->grow(sink->ctx, raw, &cap); // the charged spill: the hint never lies here,
        if (buf == NULL || cap < raw)           // but the sink's reserve may still under-deliver
            return -1;
    }
    if (raw > 0 && pack_read_chunks(pack, e, 0, raw, buf) != 0)
        return -1;
    if (sink->commit)
        sink->commit(sink->ctx, raw);
    if (out_size)
        *out_size = raw;
    return 0;
}

// Ranged read: exactly [off, off+len) or refusal. 0 / RES_RANGE_EOF / -1. Never a silent partial.
int res_pack_read_range(const res_pack *pack, uint32_t entry, uint64_t off, size_t len,
                        void *dst)
{
    if (pack == NULL || pack->toc == NULL || entry >= pack->hdr.entry_count
        || (dst == NULL && len > 0))
        return -1;
    const ano_pack_entry *e = &pack->toc[entry];
    if (off > e->raw_size || len > e->raw_size - off)
        return RES_RANGE_EOF;
    if (len == 0)
        return 0;
    return pack_read_chunks(pack, e, off, len, dst);
}

/* DETERMINISTIC builder */

// Same input set -> same bytes. Sorted by rid. Offsets pure. Padding zeroed.

typedef struct pak_buf { uint8_t *p; size_t n, cap; } pak_buf;

static int pak_buf_reserve(pak_buf *b, size_t extra)
{
    if (extra <= b->cap - b->n)
        return 0;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap - b->n < extra) {
        if (cap > SIZE_MAX / 2)
            return -1;
        cap *= 2;
    }
    uint8_t *q = mi_realloc(b->p, cap);
    if (q == NULL)
        return -1;
    b->p = q;
    b->cap = cap;
    return 0;
}

static int pak_buf_put(pak_buf *b, const void *data, size_t len)
{
    if (pak_buf_reserve(b, len) != 0)
        return -1;
    memcpy(b->p + b->n, data, len);
    b->n += len;
    return 0;
}

static int pak_buf_zeros(pak_buf *b, size_t len)
{
    if (pak_buf_reserve(b, len) != 0)
        return -1;
    memset(b->p + b->n, 0, len);
    b->n += len;
    return 0;
}

typedef struct pak_item {
    char    *logical;                           // mi_malloc'd, NUL-terminated
    size_t   len;
    uint64_t rid, rid2;
    uint32_t tag;
} pak_item;

typedef struct pak_tree {
    pak_item   *v;
    size_t      n, cap;
    const char *root;
} pak_tree;

typedef struct pak_names {
    char  **v;
    size_t  n, cap;
    int     err;
} pak_names;

static void pak_names_cb(const char *name, void *ctx)
{
    pak_names *ns = ctx;
    if (ns->err)
        return;
    if (ns->n == ns->cap) {
        size_t cap = ns->cap ? ns->cap * 2 : 16;
        char **q = mi_realloc(ns->v, cap * sizeof *q);
        if (q == NULL) { ns->err = -1; return; }
        ns->v = q;
        ns->cap = cap;
    }
    size_t len = strlen(name);
    char *copy = mi_malloc(len + 1);
    if (copy == NULL) { ns->err = -1; return; }
    memcpy(copy, name, len + 1);
    ns->v[ns->n++] = copy;
}

static void pak_names_free(pak_names *ns)
{
    for (size_t i = 0; i < ns->n; i++)
        mi_free(ns->v[i]);
    mi_free(ns->v);
    *ns = (pak_names){0};
}

// Recursive tree walk. rel is "" at root. Invalid names SKIPPED with WARN. Order does not matter.
static int pak_walk(pak_tree *t, const char *rel)
{
    char abs[MAXPATH * 2];
    int w = rel[0] ? snprintf(abs, sizeof abs, "%s/%s", t->root, rel)
                   : snprintf(abs, sizeof abs, "%s", t->root);
    if (w < 0 || w >= (int)sizeof abs)
        return -1;
    pak_names ns = {0};
    if (rmos_scan_dir(abs, pak_names_cb, &ns) != 0 || ns.err) {
        pak_names_free(&ns);
        return -1;
    }
    int rc = 0;
    for (size_t i = 0; i < ns.n && rc == 0; i++) {
        char rel2[MAXPATH];
        int r = rel[0] ? snprintf(rel2, sizeof rel2, "%s/%s", rel, ns.v[i])
                       : snprintf(rel2, sizeof rel2, "%s", ns.v[i]);
        if (r < 0 || r >= (int)sizeof rel2) {
            ano_log(ANO_WARN, "respack: path too long, skipped: %s", ns.v[i]);
            continue;
        }
        char abs2[MAXPATH * 2];
        w = snprintf(abs2, sizeof abs2, "%s/%s", t->root, rel2);
        if (w < 0 || w >= (int)sizeof abs2)
            continue;
        uint64_t sz;
        if (rmos_stat_hint(abs2, NULL, &sz) == 0) {         // a regular file
            size_t llen;
            if (res_path_validate(rel2, &llen) != 0) {
                ano_log(ANO_WARN, "respack: bad logical path, skipped: %s", rel2);
                continue;
            }
            if (t->n == t->cap) {
                size_t cap = t->cap ? t->cap * 2 : 32;
                pak_item *q = mi_realloc(t->v, cap * sizeof *q);
                if (q == NULL) { rc = -1; break; }
                t->v = q;
                t->cap = cap;
            }
            char *copy = mi_malloc(llen + 1);
            if (copy == NULL) { rc = -1; break; }
            memcpy(copy, rel2, llen + 1);
            t->v[t->n++] = (pak_item){
                .logical = copy, .len = llen,
                .rid  = res_rid_file(rel2, llen),
                .rid2 = res_rid_file2(rel2, llen),
                .tag  = res_tag_from_path(rel2, llen),
            };
        } else if (pak_walk(t, rel2) != 0) {
            ano_log(ANO_WARN, "respack: not a file or directory, skipped: %s", rel2);
        }
    }
    pak_names_free(&ns);
    return rc;
}

static int pak_item_cmp(const void *a, const void *b)
{
    const pak_item *x = a, *y = b;
    return x->rid < y->rid ? -1 : x->rid > y->rid ? 1 : 0;
}

// Already-compressed bypass: entropy-coded entries stored RAW.
static bool pak_codec_bypass(const char *logical, uint32_t tag)
{
    (void)logical;
    return tag == RES_TAG_IMAGE_ENC;
}

// Inputs: source tree, output path, default codec. Output: 0 durable pack / -1. Duplicate rids REFUSED.
int res_pack_build(const char *src_dir, const char *out_pack, uint8_t codec)
{
    if (src_dir == NULL || out_pack == NULL || !res_codec_available((res_codec_id)codec))
        return -1;

    pak_tree t = { .root = src_dir };
    pak_buf data = {0};
    uint8_t *toc = NULL, *encbuf = NULL;
    int rc = -1;

    if (pak_walk(&t, "") != 0) {
        ano_log(ANO_ERROR, "respack: cannot enumerate %s", src_dir);
        goto out;
    }
    // The output must never enumerate as its own input: building into the source tree would
    // otherwise embed the previous archive and grow the pack on every identical command.
    {
        const char *self = out_pack;
        if (self[0] == '.' && (self[1] == '/' || self[1] == '\\'))
            self += 2;
        size_t rootlen = strlen(src_dir);
        if (strncmp(self, src_dir, rootlen) == 0
            && (self[rootlen] == '/' || self[rootlen] == '\\')) {
            const char *rel = self + rootlen + 1;
            for (size_t i = 0; i < t.n; i++) {
                if (strcmp(t.v[i].logical, rel) == 0) {
                    mi_free(t.v[i].logical);
                    t.v[i] = t.v[--t.n];
                    break;
                }
            }
        }
    }
    if (t.n > PACK_ENTRY_MAX)
        goto out;
    qsort(t.v, t.n, sizeof *t.v, pak_item_cmp);
    for (size_t i = 1; i < t.n; i++) {
        if (t.v[i].rid == t.v[i - 1].rid) {
            ano_log(ANO_ERROR, "respack: rid collision: '%s' vs '%s' -- REFUSED",
                    t.v[i - 1].logical, t.v[i].logical);
            goto out;
        }
    }

    size_t ebound = res_codec_bound((res_codec_id)codec, RES_CODEC_CHUNK);
    if (ebound > 0 && (encbuf = mi_malloc(ebound)) == NULL)
        goto out;
    uint64_t data_base = 32u + (uint64_t)t.n * 48u + 8u;    // header + TOC + toc_hash
    toc = mi_malloc(t.n * 48u + 1u);                        // +1: n == 0 stays a live pointer
    if (toc == NULL)
        goto out;

    for (size_t i = 0; i < t.n; i++) {
        pak_item *it = &t.v[i];
        char abs[MAXPATH * 2];
        int w = snprintf(abs, sizeof abs, "%s/%s", src_dir, it->logical);
        if (w < 0 || w >= (int)sizeof abs)
            goto out;
        void *bytes = NULL;
        size_t size = 0;
        if (res_read_all(NULL, abs, &bytes, &size) != 0) {  // EOF-truth read; enumerated files must open
            ano_log(ANO_ERROR, "respack: read failed: %s", abs);
            goto out;
        }
        uint8_t ecodec = pak_codec_bypass(it->logical, it->tag) ? RES_CODEC_RAW : codec;
        uint64_t nchunks = chunk_count_of(size);

        // Deterministic alignment padding, then the index placeholder, then the chunks.
        uint64_t at = data_base + data.n;
        size_t pad = (size_t)((PACK_DATA_ALIGN - at % PACK_DATA_ALIGN) % PACK_DATA_ALIGN);
        if (pak_buf_zeros(&data, pad) != 0) { mi_free(bytes); goto out; }
        uint64_t data_off = data_base + data.n;
        size_t entry_start = data.n;
        size_t index_pos = data.n;
        if (pak_buf_zeros(&data, (size_t)nchunks * PACK_CHUNK_REC) != 0) { mi_free(bytes); goto out; }

        for (uint64_t c = 0; c < nchunks; c++) {
            const uint8_t *src = (const uint8_t *)bytes + c * RES_CODEC_CHUNK;
            size_t rlen = size - c * RES_CODEC_CHUNK > RES_CODEC_CHUNK
                        ? RES_CODEC_CHUNK : size - (size_t)(c * RES_CODEC_CHUNK);
            const uint8_t *out_bytes = src;
            size_t stored = rlen;
            uint32_t flag = PACK_CHUNK_RAW;
            if (ecodec != RES_CODEC_RAW) {
                size_t n = res_codec_encode((res_codec_id)ecodec, src, rlen, encbuf, ebound);
                if (n > 0) {                    // 0 = not smaller: the per-chunk RAW fallback
                    out_bytes = encbuf;
                    stored = n;
                    flag = 0;
                }
            }
            // Record BEFORE the append: pak_buf_put may move data.p.
            uint64_t rel = data.n - entry_start;
            uint8_t *rec = data.p + index_pos + (size_t)c * PACK_CHUNK_REC;
            put64(rec, rel);
            put32(rec + 8, (uint32_t)stored | flag);
            put32(rec + 12, fold32(res_fnv1a64(out_bytes, stored)));
            if (pak_buf_put(&data, out_bytes, stored) != 0) { mi_free(bytes); goto out; }
        }
        mi_free(bytes);

        uint8_t *te = toc + i * 48u;
        put64(te + 0, it->rid);
        put64(te + 8, it->rid2);
        put64(te + 16, data_off);
        put64(te + 24, (uint64_t)size);
        put64(te + 32, (uint64_t)(data.n - entry_start));
        put32(te + 40, it->tag);
        te[44] = ecodec;
        te[45] = 0;                             // ANO_PACK_ENTRY_BLOCK is the bake path's, later
        put16(te + 46, 0);
    }

    uint8_t hb[32];
    put32(hb + 0, ANO_PACK_MAGIC);
    put16(hb + 4, ANO_PACK_VERSION);
    hb[6] = codec;
    hb[7] = ANO_PACK_FLAG_SORTED;
    put32(hb + 8, (uint32_t)t.n);
    put32(hb + 12, 0);
    put64(hb + 16, 32u);
    put64(hb + 24, res_fnv1a64(hb, 24));
    uint8_t th[8];
    put64(th, res_fnv1a64(t.n ? toc : NULL, t.n * 48u));

    // Durable, atomic emit. res_write_protocol wants a parent path; anchor bare names to ".".
    char final[MAXPATH * 2];
    int w = strchr(out_pack, '/') ? snprintf(final, sizeof final, "%s", out_pack)
                                  : snprintf(final, sizeof final, "./%s", out_pack);
    if (w < 0 || w >= (int)sizeof final)
        goto out;
    res_iovec parts[4] = {
        { hb, sizeof hb }, { toc, t.n * 48u }, { th, sizeof th }, { data.p, data.n },
    };
    if (res_write_protocol(final, parts, 4) != 0) {
        ano_log(ANO_ERROR, "respack: write failed: %s", final);
        goto out;
    }
    rc = 0;
out:
    for (size_t i = 0; i < t.n; i++)
        mi_free(t.v[i].logical);
    mi_free(t.v);
    mi_free(data.p);
    mi_free(toc);
    mi_free(encbuf);
    return rc;
}

int ano_res_mount_pack(const char *prefix, ano_fspath pack_file)
{
    return res_pack_mount(prefix, pack_file);
}

int ano_res_pack_build(const char *src_dir, const char *out_pack)
{
    return res_pack_build(src_dir, out_pack, RES_CODEC_LZ4);
}
