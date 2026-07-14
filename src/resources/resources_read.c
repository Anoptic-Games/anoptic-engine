/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The read side: the candidate walk, the destination-aware sink, source dispatch, and
// ranges. Split out of resources_core.c so the write protocol and the read path stop
// sharing a file (and a workstream). Mount state stays in resources_core.c; this file
// reads it through the frozen accessors.

#include <anoptic_resources.h>

#include <anoptic_log.h>

#include <string.h>

#include "resources_internal.h"
#include "codec/res_codec.h"
#include "pack/res_pack.h"

// ---------------------------------------------------------------------------------------------
// The namespace walk. Write root, then mounts newest-first, then the base mount.
//
// TODO(W4, M14): res_candidates_ex makes this TWO passes -- every DIR candidate, THEN every
// PACK candidate -- so loose-shadows-pack becomes an invariant of the WALK. Today's single
// pass is write-root > mounts-newest-first > base, and a pack mounted as a mount would
// SHADOW the loose base: the exact inverse of the requirement.

static bool prefix_applies(anostr_t prefix, const char *logical, size_t len,
                           const char **rem, size_t *rem_len)
{
    size_t plen = anostr_len(prefix);
    if (plen == 0) {
        *rem = logical;
        *rem_len = len;
        return true;
    }
    if (len <= plen || memcmp(anostr_bytes(&prefix), logical, plen) != 0)
        return false;
    *rem = logical + plen;
    *rem_len = len - plen;
    return true;
}

int res_candidates(const char *logical, size_t len, ano_fspath *out, int cap)
{
    if (!res_ready() || cap <= 0)
        return 0;
    int n = 0;
    ano_fspath wroot = res_write_root();
    ano_fspath p = res_join(&wroot, logical, len);
    if (p.length && n < cap)
        out[n++] = p;
    for (int i = res_mount_count() - 1; i >= 0; i--) {      // newest-first
        const char *rem;
        size_t rlen;
        if (!prefix_applies(res_mount_prefix(i), logical, len, &rem, &rlen))
            continue;
        ano_fspath root = res_mount_root(i);
        p = res_join(&root, rem, rlen);
        if (p.length && n < cap)
            out[n++] = p;
    }
    ano_fspath base = res_base_root();
    p = res_join(&base, logical, len);
    if (p.length && n < cap)
        out[n++] = p;
    return n;
}

// Pass 1 emits every DIR candidate; pass 2 every PACK candidate. STUB: pass 2 is empty
// until packs mount, so this is res_candidates wearing the source type.
int res_candidates_ex(const char *logical, size_t len, res_source *out, int cap)
{
    if (out == NULL || cap <= 0)
        return 0;
    ano_fspath dirs[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, dirs, ANO_RES_MAX_MOUNTS + 2);
    int k = 0;
    for (int i = 0; i < n && k < cap; i++)
        out[k++] = (res_source){ .kind = RES_SRC_DIR, .path = dirs[i] };
    // TODO(W4, M14): pass 2 -- every mounted pack that claims this prefix, in mount order.
    return k;
}

// ---------------------------------------------------------------------------------------------
// The gulp primitive and the unowned read.
//
// TODO(W4/W2, M10): res_read_all becomes a res_read_sink wrapper and ano_res_get reads into
// its planned HOME with a charged spill path. Direct landing is only sound WITH a spill,
// because rmos_size_hint is a hint by deliberate design and a multipool class block cannot
// grow in place. EOF-truth is preserved verbatim through that change.

int res_read_all(mi_heap_t *heap, const char *abs, void **out, size_t *out_size)
{
    rmos_file f;
    if (rmos_read_open(abs, &f) != 0)
        return -1;

    int64_t hint = rmos_size_hint(f);           // a HINT: the loop believes only EOF
    size_t cap = hint > 0 ? (size_t)hint + 1 : 4096;
    uint8_t *buf = heap ? mi_heap_malloc_aligned(heap, cap, ANO_CACHE_LINE)
                        : mi_malloc_aligned(cap, ANO_CACHE_LINE);
    if (buf == NULL) {
        rmos_read_close(f);
        return -2;
    }
    size_t size = 0;
    for (;;) {
        size_t space = cap - 1 - size;
        if (space == 0) {
            if (cap > SIZE_MAX / 2) {
                mi_free(buf);
                rmos_read_close(f);
                return -2;
            }
            uint8_t *grown = heap ? mi_heap_realloc_aligned(heap, buf, cap * 2, ANO_CACHE_LINE)
                                  : mi_realloc_aligned(buf, cap * 2, ANO_CACHE_LINE);
            if (grown == NULL) {
                mi_free(buf);
                rmos_read_close(f);
                return -2;
            }
            buf = grown;
            cap *= 2;
            space = cap - 1 - size;
        }
        size_t want = space < RMOS_CHUNK_MAX ? space : RMOS_CHUNK_MAX;
        size_t got  = 0;
        if (rmos_read_chunk(f, buf + size, want, &got) != 0) {
            mi_free(buf);
            rmos_read_close(f);
            return -2;
        }
        if (got == 0)
            break;                              // EOF, the only truth
        size += got;
    }
    rmos_read_close(f);
    buf[size] = 0;                              // the guard NUL
    if (cap > size + 1) {
        uint8_t *tight = heap ? mi_heap_realloc_aligned(heap, buf, size + 1, ANO_CACHE_LINE)
                              : mi_realloc_aligned(buf, size + 1, ANO_CACHE_LINE);
        if (tight != NULL)
            buf = tight;
    }
    *out      = buf;
    *out_size = size;
    return 0;
}

anostr_t ano_res_slurp(mi_heap_t *heap, const char *logical)
{
    size_t len;
    if (heap == NULL || res_path_validate(logical, &len) != 0) {
        ano_log(ANO_ERROR, "resources: slurp refused (bad heap or path)");
        return anostr_empty();
    }
    if (!res_ready()) {
        ano_log(ANO_ERROR, "resources: slurp before init: %s", logical);
        return anostr_empty();
    }
    res_freeze();
    ano_fspath cand[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, cand, ANO_RES_MAX_MOUNTS + 2);
    for (int i = 0; i < n; i++) {
        void  *buf  = NULL;
        size_t size = 0;
        int rc = res_read_all(heap, cand[i].str, &buf, &size);
        if (rc == -1)
            continue;                           // this root cannot even open it
        if (rc != 0) {
            ano_log(ANO_ERROR, "resources: read failed mid-file: %s", cand[i].str);
            return anostr_empty();
        }
        if (size > UINT32_MAX) {
            ano_log(ANO_ERROR, "resources: slurp cannot express %zu bytes: %s",
                    size, logical);
            mi_free(buf);
            return anostr_empty();
        }
        anostr_t v = anostr_view(buf, size);
        if (size <= ANOSTR_INLINE_CAP)
            mi_free(buf);                       // the value is self-contained now
        return v;
    }
    ano_log(ANO_ERROR, "resources: not found in any mount: %s", logical);
    return anostr_empty();
}

// ---------------------------------------------------------------------------------------------
// The destination-aware read. STUB.
//
// TODO(W4, M10): reserve() may hand back the resource's planned HOME block, so an IO read
// lands where the resource will live; grow() is the CHARGED spill when the size hint lied
// (hint_mismatch_copies counts exactly that). gfx_slurp and save_probe_file migrate onto it.

int res_read_sink(const res_sink *sink, const char *abs, size_t *out_size)
{
    (void)sink; (void)abs; (void)out_size;
    return -1;                                  // TODO(W4, M10)
}

int res_source_read_sink(const res_source *src, const res_sink *sink, size_t *out_size)
{
    (void)src; (void)sink; (void)out_size;
    return -1;                                  // TODO(W4, M10)
}

// ---------------------------------------------------------------------------------------------
// Ranges and hashes.
//
// 0 / RES_RANGE_EOF / -1 / -2 -- never a silent partial. res_hash_file is what hot reload
// CONFIRMS with, because mtime lies on 9P and SMB.

// Streaming FNV-1a-64: fold `len` bytes into a running hash seeded with res_fnv1a64's basis.
static uint64_t fnv1a64_acc(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

// Deliver exactly len bytes of a DIR source at off. The seam reports short reads; the loop
// here owns termination: only *got == 0 (true EOF) ends it early, and early is RES_RANGE_EOF.
static int dir_read_range(const char *abs, uint64_t off, size_t len, void *dst)
{
    rmos_file f;
    if (rmos_read_open(abs, &f) != 0)
        return -1;
    uint8_t *p = dst;
    size_t done = 0;
    while (done < len) {
        size_t want = len - done;
        if (want > RMOS_CHUNK_MAX)
            want = RMOS_CHUNK_MAX;
        size_t got = 0;
        if (rmos_read_at(f, off + done, p + done, want, &got) != 0) {
            rmos_read_close(f);
            return -1;
        }
        if (got == 0) {                         // the ONLY EOF signal: the range ran past the end
            rmos_read_close(f);
            return RES_RANGE_EOF;
        }
        done += got;
    }
    rmos_read_close(f);
    return 0;
}

// Inputs: a source, an absolute range, a destination of at least len bytes. Output: 0 with
// exactly len bytes delivered / RES_RANGE_EOF when the range exceeds the file / -1 IO error /
// -2 invalid arguments. len == 0 is trivially delivered without IO.
int res_read_range(const res_source *src, uint64_t off, size_t len, void *dst)
{
    if (src == NULL || (dst == NULL && len > 0))
        return -2;
    if (len > 0 && off + (uint64_t)len < off)
        return -2;                              // off + len overflows: no file holds it
    if (len == 0)
        return 0;
    switch (src->kind) {
    case RES_SRC_DIR:
        if (src->path.length == 0)
            return -2;
        return dir_read_range(src->path.str, off, len, dst);
    case RES_SRC_PACK:
        return res_pack_read_range(src->pack, src->entry, off, len, dst);
    default:
        return -2;
    }
}

// FNV-1a-64 over the WHOLE source in bounded chunks, plus its total size. For a PACK source
// the hash covers the DECODED bytes -- the same bytes a loose file would deliver -- so hot
// reload's confirm step compares like with like. 0 / -1.
int res_hash_file(const res_source *src, uint64_t *hash, uint64_t *size)
{
    if (src == NULL)
        return -1;
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    uint64_t total = 0;
    uint8_t *buf = mi_malloc(RES_CODEC_CHUNK);
    if (buf == NULL)
        return -1;
    if (src->kind == RES_SRC_DIR) {
        rmos_file f;
        if (src->path.length == 0 || rmos_read_open(src->path.str, &f) != 0) {
            mi_free(buf);
            return -1;
        }
        for (;;) {
            size_t got = 0;
            if (rmos_read_chunk(f, buf, RES_CODEC_CHUNK, &got) != 0) {
                rmos_read_close(f);
                mi_free(buf);
                return -1;
            }
            if (got == 0)
                break;                          // EOF, the only truth
            h = fnv1a64_acc(h, buf, got);
            total += got;
        }
        rmos_read_close(f);
    } else if (src->kind == RES_SRC_PACK) {
        if (src->pack == NULL || src->entry >= src->pack->hdr.entry_count) {
            mi_free(buf);
            return -1;
        }
        uint64_t raw = src->pack->toc[src->entry].raw_size;
        // Chunk-aligned steps: each res_pack_read_range decodes exactly the chunks it touches.
        while (total < raw) {
            size_t want = raw - total > RES_CODEC_CHUNK ? RES_CODEC_CHUNK
                                                        : (size_t)(raw - total);
            if (res_pack_read_range(src->pack, src->entry, total, want, buf) != 0) {
                mi_free(buf);
                return -1;
            }
            h = fnv1a64_acc(h, buf, want);
            total += want;
        }
    } else {
        mi_free(buf);
        return -1;
    }
    mi_free(buf);
    if (hash) *hash = h;
    if (size) *size = total;
    return 0;
}

// Public boundary: 0 / -1 / ANO_RES_RANGE_EOF -- invalid arguments fold into -1 out here.
// The first PRESENT candidate decides; a mid-file failure is loud, never a silent fallback.
int ano_res_read_range(const char *logical, uint64_t off, size_t len, void *dst)
{
    size_t llen;
    if (res_path_validate(logical, &llen) != 0 || (dst == NULL && len > 0) || !res_ready())
        return -1;
    res_freeze();
    res_source srcs[ANO_RES_MAX_MOUNTS + 2 + ANO_PACK_MAX];
    int n = res_candidates_ex(logical, llen, srcs, ANO_RES_MAX_MOUNTS + 2 + ANO_PACK_MAX);
    for (int i = 0; i < n; i++) {
        // rmos_exists is ADVISORY: it only picks the candidate. If the file vanishes between
        // this probe and the read, the read fails loudly rather than falling through.
        if (srcs[i].kind == RES_SRC_DIR && !rmos_exists(srcs[i].path.str))
            continue;
        int rc = res_read_range(&srcs[i], off, len, dst);
        return rc == -2 ? -1 : rc;
    }
    return -1;
}

anores_t ano_res_get_range(ano_res_lifetime lifetime, const char *logical,
                           uint64_t off, size_t len)
{
    (void)lifetime; (void)logical; (void)off; (void)len;
    return (anores_t){0};                       // TODO(W4, M14)
}
