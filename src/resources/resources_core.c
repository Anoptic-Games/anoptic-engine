/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The platform-free resource core: namespace and mounts, the logical-path grammar, the
// gulp primitive, the durable write protocol, and gamesave commits. Registry/handles
// live in resources_registry.c; parsing lives under graphics/. OS calls go through
// resources_os.h only.

#include <anoptic_resources.h>

#include <anoptic_log.h>
#include <anoptic_threads.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "resources_internal.h"

// ---------------------------------------------------------------------------------------------
// State. Written on the main thread at init/mount, frozen at first read, read-only
// forever after -- lock-free resolution by construction. The save mutex is the one
// lock, serializing save commits.

typedef struct res_mount {
    ano_fspath root;
    anostr_t   prefix;      // canonical: empty, or "seg/.../" with one trailing '/'
} res_mount;

static struct {
    bool  init_done;
    int   init_result;
    ano_fspath write_root;
    ano_fspath base;
    res_mount  mounts[ANO_RES_MAX_MOUNTS];
    int        mount_count;
    mi_heap_t *heap;                    // module heap: interned prefixes live here
    anostr_intern_t  *intern;
    anothread_mutex_t save_mtx;
} g_res;

static _Atomic bool     g_frozen;
static _Atomic uint32_t g_tmp_counter = 1;

#ifdef ANO_RES_FAULT_INJECT
void (*res_fault_hook)(res_fault_step step);
#endif

bool res_ready(void)            { return g_res.init_done && g_res.init_result == 0; }
void res_save_lock(void)        { ano_mutex_lock(&g_res.save_mtx); }
void res_save_unlock(void)      { ano_mutex_unlock(&g_res.save_mtx); }
void res_freeze(void)           { atomic_store(&g_frozen, true); }
ano_fspath res_write_root(void) { return res_ready() ? g_res.write_root : (ano_fspath){0}; }

// ---------------------------------------------------------------------------------------------
// Path grammar.

static bool byte_ok(unsigned char c)
{
    return c >= 0x20 && c != 0x7f && c != '\\' && c != ':';
}

// One segment of [seg, seg + n): nonempty, not "." or "..".
static bool segment_ok(const char *seg, size_t n)
{
    if (n == 0)
        return false;
    if (seg[0] == '.' && (n == 1 || (n == 2 && seg[1] == '.')))
        return false;
    return true;
}

int res_path_validate(const char *logical, size_t *out_len)
{
    if (logical == NULL)
        return -1;
    size_t len = strlen(logical);
    if (len == 0 || len >= MAXPATH)
        return -1;
    size_t seg = 0;
    for (size_t i = 0; i < len; i++) {
        char c = logical[i];
        if (c == '/') {
            if (!segment_ok(logical + seg, i - seg))
                return -1;
            seg = i + 1;
            continue;
        }
        if (!byte_ok((unsigned char)c))
            return -1;
    }
    if (!segment_ok(logical + seg, len - seg))   // catches trailing '/' and lone dots
        return -1;
    if (out_len)
        *out_len = len;
    return 0;
}

int res_segment_validate(const char *seg, size_t *out_len)
{
    if (seg == NULL)
        return -1;
    size_t len = strlen(seg);
    if (len == 0 || len >= MAXPATH)
        return -1;
    for (size_t i = 0; i < len; i++)
        if (seg[i] == '/' || !byte_ok((unsigned char)seg[i]))
            return -1;
    if (!segment_ok(seg, len))
        return -1;
    if (out_len)
        *out_len = len;
    return 0;
}

int res_prefix_canon(const char *prefix, char *out, size_t *out_len)
{
    if (prefix == NULL)
        return -1;
    size_t len = strlen(prefix);
    if (len == 0) {
        out[0] = '\0';
        *out_len = 0;
        return 0;
    }
    // Accept one trailing '/', validate the body as a logical path, emit with '/'.
    size_t body = len;
    if (prefix[len - 1] == '/')
        body = len - 1;
    if (body == 0 || body + 1 >= MAXPATH)
        return -1;
    char tmp[MAXPATH];
    memcpy(tmp, prefix, body);
    tmp[body] = '\0';
    size_t vlen;
    if (res_path_validate(tmp, &vlen) != 0)
        return -1;
    memcpy(out, prefix, body);
    out[body]     = '/';
    out[body + 1] = '\0';
    *out_len = body + 1;
    return 0;
}

ano_fspath res_join(const ano_fspath *root, const char *rel, size_t rel_len)
{
    ano_fspath r = {0};
    if (root->length == 0 || rel_len == 0)
        return r;
    size_t total = (size_t)root->length + 1 + rel_len;
    if (total > MAXPATH - 1)
        return r;
    memcpy(r.str, root->str, root->length);
    r.str[root->length] = '/';
    memcpy(r.str + root->length + 1, rel, rel_len);
    r.str[total] = '\0';
    r.length = (uint16_t)total;
    return r;
}

// ---------------------------------------------------------------------------------------------
// The namespace walk.

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
    ano_fspath p = res_join(&g_res.write_root, logical, len);
    if (p.length && n < cap)
        out[n++] = p;
    for (int i = g_res.mount_count - 1; i >= 0; i--) {      // newest-first
        const char *rem;
        size_t rlen;
        if (!prefix_applies(g_res.mounts[i].prefix, logical, len, &rem, &rlen))
            continue;
        p = res_join(&g_res.mounts[i].root, rem, rlen);
        if (p.length && n < cap)
            out[n++] = p;
    }
    p = res_join(&g_res.base, logical, len);
    if (p.length && n < cap)
        out[n++] = p;
    return n;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle.

int ano_res_init(void)
{
    if (g_res.init_done)
        return g_res.init_result;
    g_res.init_done   = true;
    g_res.init_result = -1;

    ano_fspath user = ano_fs_userpath();
    if (user.length == 0) {
        ano_log(ANO_ERROR, "resources: init failed, no user-data root");
        return -1;
    }
    ano_fspath game = ano_fs_gamepath();
    if (game.length == 0) {
        ano_log(ANO_ERROR, "resources: init failed, no game root");
        return -1;
    }
    ano_fspath base = res_join(&game, "resources", 9);
    if (base.length == 0) {
        ano_log(ANO_ERROR, "resources: init failed, base mount path overflows");
        return -1;
    }
    mi_heap_t *heap = mi_heap_new();
    if (heap == NULL) {
        ano_log(ANO_ERROR, "resources: init failed, no module heap");
        return -1;
    }
    anostr_intern_t *intern = anostr_intern_make(heap);
    if (intern == NULL || ano_mutex_init(&g_res.save_mtx, NULL) != 0) {
        ano_log(ANO_ERROR, "resources: init failed, intern table or mutex");
        mi_heap_destroy(heap);
        return -1;
    }
    g_res.write_root  = user;
    g_res.base        = base;
    g_res.heap        = heap;
    g_res.intern      = intern;
    g_res.mount_count = 0;
    atomic_store(&g_frozen, false);
    if (res_registry_init() != 0) {
        ano_log(ANO_ERROR, "resources: init failed, registry");
        mi_heap_destroy(heap);
        g_res.heap = NULL;
        return -1;
    }
    g_res.init_result = 0;
    return 0;
}

int ano_res_mount(const char *prefix, ano_fspath root)
{
    if (!res_ready()) {
        ano_log(ANO_ERROR, "resources: mount before init");
        return -1;
    }
    if (atomic_load(&g_frozen)) {
        ano_log(ANO_ERROR, "resources: mount after the namespace froze");
        return -1;
    }
    if (root.length == 0 || g_res.mount_count >= ANO_RES_MAX_MOUNTS)
        return -1;
    char canon[MAXPATH];
    size_t clen;
    if (res_prefix_canon(prefix, canon, &clen) != 0)
        return -1;
    anostr_t interned = anostr_empty();
    if (clen > 0) {
        // Not dedupe: its graceful degradation would hand back the stack-borrowing view.
        anostr_sym sym = anostr_intern(g_res.intern, anostr_view(canon, clen));
        if (sym == ANOSTR_SYM_NONE)
            return -1;
        interned = anostr_sym_str(g_res.intern, sym);
    }
    g_res.mounts[g_res.mount_count].root   = root;
    g_res.mounts[g_res.mount_count].prefix = interned;
    g_res.mount_count++;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Resolution.

ano_fspath ano_res_resolve(const char *logical)
{
    ano_fspath none = {0};
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready())
        return none;
    res_freeze();
    ano_fspath cand[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, cand, ANO_RES_MAX_MOUNTS + 2);
    for (int i = 0; i < n; i++)
        if (rmos_exists(cand[i].str))
            return cand[i];
    return none;
}

ano_fspath ano_res_resolve_write(const char *logical)
{
    ano_fspath none = {0};
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready())
        return none;
    ano_fspath final = res_join(&g_res.write_root, logical, len);
    if (final.length == 0)
        return none;
    // Create the parents (the write root itself already exists).
    char parent[MAXPATH];
    const char *slash = strrchr(final.str, '/');
    if (slash != NULL && slash != final.str) {
        size_t plen = (size_t)(slash - final.str);
        memcpy(parent, final.str, plen);
        parent[plen] = '\0';
        if (rmos_mkdir_p(parent) != 0)
            return none;
    }
    return final;
}

ano_fspath ano_res_subpath(ano_fspath base, const char *relative)
{
    ano_fspath none = {0};
    size_t len;
    if (base.length == 0 || base.length >= MAXPATH || base.str[base.length] != '\0')
        return none;
    if (res_path_validate(relative, &len) != 0)
        return none;
    return res_join(&base, relative, len);
}

bool ano_res_exists(const char *logical)
{
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready())
        return false;
    res_freeze();
    ano_fspath cand[ANO_RES_MAX_MOUNTS + 2];
    int n = res_candidates(logical, len, cand, ANO_RES_MAX_MOUNTS + 2);
    for (int i = 0; i < n; i++)
        if (rmos_exists(cand[i].str))
            return true;
    return false;
}

// ---------------------------------------------------------------------------------------------
// The gulp primitive and the unowned read.

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
// The durable write protocol.

int res_write_protocol(const char *final_abs, const res_iovec *parts, int nparts)
{
    char parent[MAXPATH];
    const char *slash = strrchr(final_abs, '/');
    if (slash == NULL || slash == final_abs)
        return -1;
    size_t plen = (size_t)(slash - final_abs);
    if (plen >= sizeof parent)
        return -1;
    memcpy(parent, final_abs, plen);
    parent[plen] = '\0';

    char tmp[MAXPATH + 16];
    rmos_file f;
    bool open_ok = false;
    for (int attempt = 0; attempt < 8; attempt++) {
        uint32_t nonce = atomic_fetch_add(&g_tmp_counter, 1);
        int w = snprintf(tmp, sizeof tmp, "%s.%08x.tmp", final_abs, nonce);
        if (w < 0 || w >= (int)sizeof tmp)
            return -1;
        int rc = rmos_open_excl(tmp, &f);
        if (rc == 0) { open_ok = true; break; }
        if (rc < 0)
            return -1;
    }
    if (!open_ok)
        return -1;

    for (int i = 0; i < nparts; i++) {
        if (parts[i].len == 0)
            continue;
        if (rmos_write_all(f, parts[i].data, parts[i].len) != 0)
            goto fail_open;
    }
    RES_FAULT(RES_FAULT_AFTER_WRITE);
    if (rmos_sync(f) != 0)                      // fsyncgate: one shot, never retried
        goto fail_open;
    RES_FAULT(RES_FAULT_AFTER_SYNC);
    if (rmos_close(f) != 0) {
        rmos_unlink(tmp);
        return -1;
    }
    RES_FAULT(RES_FAULT_AFTER_CLOSE);
    if (rmos_rename_replace(tmp, final_abs) != 0) {
        rmos_unlink(tmp);
        return -1;
    }
    RES_FAULT(RES_FAULT_AFTER_RENAME);
    if (rmos_sync_dir(parent) != 0)
        ano_log(ANO_ERROR, "resources: parent-dir fsync failed after replacing %s "
                           "(rename landed; the directory entry may not be durable yet)",
                final_abs);
    return 0;

fail_open:
    rmos_close(f);
    rmos_unlink(tmp);
    return -1;
}

int ano_res_write(const char *logical, const void *data, size_t size)
{
    size_t len;
    if (res_path_validate(logical, &len) != 0 || (data == NULL && size != 0)) {
        ano_log(ANO_ERROR, "resources: write refused (bad path or buffer)");
        return -1;
    }
    ano_fspath final = ano_res_resolve_write(logical);
    if (final.length == 0) {
        ano_log(ANO_ERROR, "resources: write target unresolvable: %s", logical);
        return -1;
    }
    res_iovec part = { data, size };
    if (res_write_protocol(final.str, &part, 1) != 0) {
        ano_log(ANO_ERROR, "resources: durable write failed: %s", logical);
        return -1;
    }
    return 0;
}

int ano_res_quarantine(const char *logical)
{
    size_t len;
    if (res_path_validate(logical, &len) != 0 || !res_ready())
        return -1;
    ano_fspath final = res_join(&g_res.write_root, logical, len);
    if (final.length == 0 || !rmos_exists(final.str))
        return -1;
    char broken[MAXPATH + 8];
    int w = snprintf(broken, sizeof broken, "%s.broken", final.str);
    if (w < 0 || w >= (int)sizeof broken)
        return -1;
    if (rmos_rename_replace(final.str, broken) != 0) {
        ano_log(ANO_ERROR, "resources: quarantine rename failed: %s", logical);
        return -1;
    }
    ano_log(ANO_WARN, "resources: quarantined %s -> %s.broken", logical, logical);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// FNV-1a-64 and the save frame.

uint64_t res_fnv1a64(const void *data, size_t len)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    if (data == NULL || len == 0)
        return h;
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> 8 * i); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = v << 8 | p[i]; return v; }
static uint64_t get64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

void res_save_frame(uint8_t hdr[RES_SAVE_HDR_BYTES], uint8_t ftr[RES_SAVE_FTR_BYTES],
                    uint32_t format_version, const void *payload, size_t payload_len,
                    uint64_t seq)
{
    memcpy(hdr, "ANOS", 4);
    put16(hdr + 4, RES_SAVE_CONTAINER_VERSION);
    hdr[6] = RES_SAVE_HASH_FNV1A64;
    hdr[7] = 0;                                 // flags
    put32(hdr + 8, format_version);
    put32(hdr + 12, format_version);            // min_reader_version, conservative
    put64(hdr + 16, (uint64_t)payload_len);
    put64(hdr + 24, seq);
    put64(hdr + 32, res_fnv1a64(hdr, 32));
    put64(hdr + 40, 0);                         // reserved

    put64(ftr, res_fnv1a64(payload, payload_len));
    memcpy(ftr + 8, "ANOSDONE", 8);
}

int res_save_validate(const uint8_t *bytes, size_t len, uint32_t *out_format_version,
                      uint64_t *out_seq, const uint8_t **out_payload,
                      size_t *out_payload_len)
{
    if (bytes == NULL || len < RES_SAVE_HDR_BYTES + RES_SAVE_FTR_BYTES)
        return -1;
    if (memcmp(bytes, "ANOS", 4) != 0)
        return -1;
    if (get16(bytes + 4) != RES_SAVE_CONTAINER_VERSION)
        return -1;
    if (bytes[6] != RES_SAVE_HASH_FNV1A64 || bytes[7] != 0)
        return -1;
    if (get64(bytes + 32) != res_fnv1a64(bytes, 32))
        return -1;
    if (get64(bytes + 40) != 0)
        return -1;
    // The header is sound from here: mismatches below are body damage.
    uint64_t payload_len = get64(bytes + 16);
    if (payload_len > SIZE_MAX - RES_SAVE_HDR_BYTES - RES_SAVE_FTR_BYTES)
        return -2;
    if (len != RES_SAVE_HDR_BYTES + (size_t)payload_len + RES_SAVE_FTR_BYTES)
        return -2;
    const uint8_t *payload = bytes + RES_SAVE_HDR_BYTES;
    const uint8_t *ftr     = payload + payload_len;
    if (memcmp(ftr + 8, "ANOSDONE", 8) != 0)
        return -2;
    if (get64(ftr) != res_fnv1a64(payload, (size_t)payload_len))
        return -2;
    if (out_format_version) *out_format_version = get32(bytes + 8);
    if (out_seq)            *out_seq            = get64(bytes + 24);
    if (out_payload)        *out_payload        = payload;
    if (out_payload_len)    *out_payload_len    = (size_t)payload_len;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Gamesave commits. Serialized on one internal mutex; every generation is a brand-new
// filename, verified through a fresh handle before anything older is pruned.

typedef struct save_scan {
    const char *slot;
    size_t      slot_len;
    uint64_t    top[ANO_RES_SAVE_KEEP];         // newest seqs, descending
    int         top_count;
    uint64_t    max_seq;
    uint64_t    victims[64];                    // seqs to prune this pass
    int         victim_count;
    uint64_t    keep_min;                       // pruning pass: keep seq >= this
    bool        pruning;
} save_scan;

bool res_save_name_seq(const char *name, const char *slot, size_t slot_len,
                       uint64_t *out_seq)
{
    size_t nlen = strlen(name);
    if (nlen < slot_len + 10)                   // '.' + digit + ".anosave"
        return false;
    if (memcmp(name, slot, slot_len) != 0 || name[slot_len] != '.')
        return false;
    const char *digits = name + slot_len + 1;
    const char *suffix = name + nlen - 8;
    if (memcmp(suffix, ".anosave", 8) != 0 || suffix <= digits)
        return false;
    if (digits[0] == '0' && suffix - digits > 1)
        return false;                           // leading zeros would alias seqs
    uint64_t v = 0;
    for (const char *p = digits; p < suffix; p++) {
        if (*p < '0' || *p > '9')
            return false;
        v = v * 10 + (uint64_t)(*p - '0');
        if (v > UINT64_MAX / 2)                 // planted max must not wrap max+1
            return false;
    }
    *out_seq = v;
    return true;
}

static void save_scan_cb(const char *name, void *ctx)
{
    save_scan *s = ctx;
    uint64_t seq;
    if (!res_save_name_seq(name, s->slot, s->slot_len, &seq))
        return;
    if (seq > s->max_seq)
        s->max_seq = seq;
    if (s->pruning) {
        if (seq < s->keep_min && s->victim_count < (int)(sizeof s->victims / sizeof s->victims[0]))
            s->victims[s->victim_count++] = seq;
        return;
    }
    // Insert into the descending top-K.
    for (int i = 0; i < ANO_RES_SAVE_KEEP; i++) {
        if (i >= s->top_count || seq > s->top[i]) {
            for (int j = (s->top_count < ANO_RES_SAVE_KEEP ? s->top_count : ANO_RES_SAVE_KEEP - 1);
                 j > i; j--)
                s->top[j] = s->top[j - 1];
            s->top[i] = seq;
            if (s->top_count < ANO_RES_SAVE_KEEP)
                s->top_count++;
            return;
        }
    }
}

static int save_commit_locked(const char *slot, size_t slot_len, uint32_t format_version,
                              const void *payload, size_t size)
{
    // saves/ under the write root, created if absent.
    ano_fspath saves = res_join(&g_res.write_root, "saves", 5);
    if (saves.length == 0)
        return -1;
    char dir[MAXPATH];
    memcpy(dir, saves.str, (size_t)saves.length + 1);
    if (rmos_mkdir_p(dir) != 0) {
        ano_log(ANO_ERROR, "resources: cannot create saves dir");
        return -1;
    }

    // Next sequence number: one past the highest ever seen for this slot. A failed
    // scan must refuse: guessing seq = 1 could rename-replace a live generation.
    save_scan scan = { .slot = slot, .slot_len = slot_len };
    if (rmos_scan_dir(saves.str, save_scan_cb, &scan) != 0) {
        ano_log(ANO_ERROR, "resources: saves dir unreadable, commit refused");
        return -1;
    }
    uint64_t seq = scan.max_seq + 1;

    char rel[MAXPATH];
    int w = snprintf(rel, sizeof rel, "saves/%s.%llu.anosave", slot,
                     (unsigned long long)seq);
    if (w < 0 || w >= (int)sizeof rel)
        return -1;
    ano_fspath final = res_join(&g_res.write_root, rel, (size_t)w);
    if (final.length == 0) {
        ano_log(ANO_ERROR, "resources: save path overflows: %s", rel);
        return -1;
    }

    uint8_t hdr[RES_SAVE_HDR_BYTES], ftr[RES_SAVE_FTR_BYTES];
    res_save_frame(hdr, ftr, format_version, payload, size, seq);
    res_iovec parts[3] = { { hdr, sizeof hdr }, { payload, size }, { ftr, sizeof ftr } };
    if (res_write_protocol(final.str, parts, 3) != 0) {
        ano_log(ANO_ERROR, "resources: save commit failed for slot %s", slot);
        return -1;
    }

    // Verify through a fresh handle before touching older generations.
    {
        mi_heap_t *scratch LOCALHEAPATTR = mi_heap_new();
        if (scratch == NULL) {
            ano_log(ANO_ERROR, "resources: save verify heap failed for %s", rel);
            return -1;
        }
        void  *bytes = NULL;
        size_t blen  = 0;
        uint32_t vfmt = 0;
        uint64_t vseq = 0;
        if (res_read_all(scratch, final.str, &bytes, &blen) != 0
            || res_save_validate(bytes, blen, &vfmt, &vseq, NULL, NULL) != 0
            || vfmt != format_version || vseq != seq) {
            ano_log(ANO_ERROR, "resources: save verify FAILED for %s; removing it, "
                               "prior generations untouched", rel);
            rmos_unlink(final.str);
            return -1;
        }
    }

    // Prune to the newest ANO_RES_SAVE_KEEP. A failed unlink only leaves extras.
    if (scan.top_count >= ANO_RES_SAVE_KEEP) {          // seq joined an already-full set
        save_scan prune = { .slot = slot, .slot_len = slot_len, .pruning = true,
                            .keep_min = scan.top[ANO_RES_SAVE_KEEP - 2] };
        // keep: seq (new), top[0], top[1] -- everything below top[KEEP-2] goes.
        rmos_scan_dir(saves.str, save_scan_cb, &prune);
        for (int i = 0; i < prune.victim_count; i++) {
            char vic[MAXPATH];
            int vw = snprintf(vic, sizeof vic, "%s/%s.%llu.anosave", saves.str, slot,
                              (unsigned long long)prune.victims[i]);
            if (vw > 0 && vw < (int)sizeof vic && rmos_unlink(vic) != 0)
                ano_log(ANO_WARN, "resources: could not prune old save %s", vic);
        }
    }
    return 0;
}

int ano_res_save_commit(const char *slot, uint32_t format_version,
                        const void *payload, size_t size)
{
    size_t slot_len;
    if (res_segment_validate(slot, &slot_len) != 0 || (payload == NULL && size != 0)) {
        ano_log(ANO_ERROR, "resources: save commit refused (bad slot or payload)");
        return -1;
    }
    if (!res_ready()) {
        ano_log(ANO_ERROR, "resources: save commit before init");
        return -1;
    }
    ano_mutex_lock(&g_res.save_mtx);
    int rc = save_commit_locked(slot, slot_len, format_version, payload, size);
    ano_mutex_unlock(&g_res.save_mtx);
    return rc;
}
