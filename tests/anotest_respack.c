/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* anopak: mount, lookup, ranged read, deterministic build
 *
 * End-to-end oracle for the pack runtime and builder over a tree this test stages itself:
 *   - res_pack_build produces a pack; a rebuild, and a build from a second tree staged in a
 *     different order, are BYTE-IDENTICAL (the format's determinism claim);
 *   - every staged entry is found by rid, its whole bytes (read_range and read_sink) and its
 *     res_hash_file digest match an independent computation over the known content;
 *   - ranged reads deliver exact slices, including one straddling a 496 KiB chunk boundary,
 *     len == 0 at off == size, and off past size -> RES_RANGE_EOF;
 *   - the corruption battery: a single flipped bit in the magic, the hashed header, the TOC,
 *     or the TOC hash is REFUSED at mount; a flipped payload bit mounts but REFUSES on read
 *     (the per-chunk hash); a TOC forged to name GDEFLATE (with a recomputed TOC hash) is
 *     refused for the unavailable codec.
 * A stub returning -1 fails at the first build. Exit 0 == pass. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_filesystem.h>
#include <anoptic_log.h>
#include <anoptic_resources.h>

#include "resources_internal.h"
#include "codec/res_codec.h"
#include "pack/res_pack.h"
#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------------------------
// Little helpers: an independent FNV-1a-64 oracle, and plain-stdio file IO for staging and
// for the corruption battery (deliberately not the code under test).

static uint64_t fnv_oracle(const uint8_t *p, size_t n)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= UINT64_C(0x100000001b3); }
    return h;
}
static uint32_t rd32(const uint8_t *p) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static void     wr64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return 0;
    size_t w = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return w == len;
}
static uint8_t *load_file(const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    size_t r = (n > 0 && b) ? fread(b, 1, (size_t)n, f) : 0;
    fclose(f);
    if (b == NULL || r != (size_t)n) { free(b); return NULL; }
    *out = (size_t)n;
    return b;
}

// ---------------------------------------------------------------------------------------------
// The staged corpus. Content is a pure function of the logical path, so both the builder and
// the verifier can regenerate it independently.

typedef struct { const char *logical; size_t len; } corpus_ent;
static const corpus_ent CORPUS[] = {
    { "a.txt",          13 },
    { "b.dat",          600 * 1024 },   // multi-chunk: 496 KiB + remainder
    { "sub/c.bin",      10 },
    { "sub/deep/d.bin", 0 },            // empty entry
    { "sub/deep/e.bin", 507904 },       // exactly one RES_CODEC_CHUNK
};
enum { NENT = (int)(sizeof CORPUS / sizeof CORPUS[0]) };

static void gen_content(const char *logical, size_t len, uint8_t *out)
{
    uint64_t s = fnv_oracle((const uint8_t *)logical, strlen(logical)) | 1;
    for (size_t i = 0; i < len; i++) {
        if ((i / 257) & 1) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; out[i] = (uint8_t)s; }
        else               { out[i] = (uint8_t)(logical[0] + (i & 0x1f)); }
    }
}

// Stage the corpus under `root`, creating parent dirs. `reverse` flips the write order (the
// filesystem enumeration the builder sees is its own business; determinism must not depend on
// the order in which we created the files).
static void stage_corpus(const char *root, int reverse)
{
    char path[512];
    scratch_make_dir(root);
    snprintf(path, sizeof path, "%s/sub", root);        scratch_make_dir(path);
    snprintf(path, sizeof path, "%s/sub/deep", root);   scratch_make_dir(path);
    for (int j = 0; j < NENT; j++) {
        int i = reverse ? NENT - 1 - j : j;
        uint8_t *buf = malloc(CORPUS[i].len ? CORPUS[i].len : 1);
        gen_content(CORPUS[i].logical, CORPUS[i].len, buf);
        snprintf(path, sizeof path, "%s/%s", root, CORPUS[i].logical);
        CHECK(write_file(path, buf, CORPUS[i].len), "stage corpus file");
        free(buf);
    }
}

static void unstage_corpus(const char *root)
{
    char path[512];
    for (int i = 0; i < NENT; i++) {
        snprintf(path, sizeof path, "%s/%s", root, CORPUS[i].logical);
        remove(path);
    }
    snprintf(path, sizeof path, "%s/sub/deep", root); scratch_remove_dir(path);
    snprintf(path, sizeof path, "%s/sub", root);      scratch_remove_dir(path);
    scratch_remove_dir(root);
}

// A fixed-buffer sink for res_pack_read_sink.
typedef struct { uint8_t *buf; size_t cap; size_t final; } fixed_sink;
static void *fs_reserve(void *ctx, size_t hint, size_t *out_cap)
{
    fixed_sink *s = ctx; *out_cap = s->cap; return s->cap >= hint ? s->buf : NULL;
}
static void fs_commit(void *ctx, size_t final) { ((fixed_sink *)ctx)->final = final; }

static ano_fspath fspath(const char *p)
{
    ano_fspath fp = {0};
    int w = snprintf(fp.str, sizeof fp.str, "%s", p);
    fp.length = (w > 0 && w < (int)sizeof fp.str) ? (uint16_t)w : 0;
    return fp;
}

// ---------------------------------------------------------------------------------------------

static void verify_entries(const res_pack *pack)
{
    static uint8_t got[600 * 1024 + 16];
    for (int i = 0; i < NENT; i++) {
        size_t len = CORPUS[i].len;
        uint8_t *want = malloc(len ? len : 1);
        gen_content(CORPUS[i].logical, len, want);

        int e = res_pack_find(pack, CORPUS[i].logical, strlen(CORPUS[i].logical));
        CHECK(e >= 0, "find staged entry");
        if (e < 0) { free(want); continue; }

        // Whole read via read_range.
        memset(got, 0xCD, len ? len : 1);
        CHECK(res_pack_read_range(pack, (uint32_t)e, 0, len, got) == 0, "read_range whole ok");
        CHECK(len == 0 || memcmp(got, want, len) == 0, "read_range whole byte-exact");

        // Whole read via read_sink.
        memset(got, 0xEF, len ? len : 1);
        fixed_sink fs = { .buf = got, .cap = sizeof got };
        res_sink sink = { .ctx = &fs, .reserve = fs_reserve, .grow = NULL, .commit = fs_commit };
        size_t osz = 123;
        CHECK(res_pack_read_sink(pack, (uint32_t)e, &sink, &osz) == 0, "read_sink ok");
        CHECK(osz == len && fs.final == len, "read_sink reports the raw size");
        CHECK(len == 0 || memcmp(got, want, len) == 0, "read_sink byte-exact");

        // res_hash_file over the PACK source: decoded-byte digest + size.
        res_source src = { .kind = RES_SRC_PACK, .pack = pack, .entry = (uint32_t)e };
        uint64_t h = 0, sz = 0;
        CHECK(res_hash_file(&src, &h, &sz) == 0, "hash_file pack ok");
        CHECK(sz == len, "hash_file pack size matches");
        CHECK(h == fnv_oracle(want, len), "hash_file pack digest matches oracle");

        free(want);
    }

    // Ranged reads on the multi-chunk entry b.dat (600 KiB, boundary at 496 KiB).
    int be = res_pack_find(pack, "b.dat", 5);
    CHECK(be >= 0, "find b.dat");
    if (be >= 0) {
        size_t len = 600 * 1024;
        uint8_t *want = malloc(len);
        gen_content("b.dat", len, want);

        // Interior slice inside chunk 0.
        CHECK(res_pack_read_range(pack, (uint32_t)be, 1000, 2000, got) == 0, "b.dat interior ok");
        CHECK(memcmp(got, want + 1000, 2000) == 0, "b.dat interior byte-exact");

        // A slice straddling the 496 KiB chunk boundary.
        uint64_t bnd = RES_CODEC_CHUNK;
        CHECK(res_pack_read_range(pack, (uint32_t)be, bnd - 300, 600, got) == 0, "b.dat straddle ok");
        CHECK(memcmp(got, want + (bnd - 300), 600) == 0, "b.dat straddle byte-exact");

        // The final byte, then EOF behaviour.
        CHECK(res_pack_read_range(pack, (uint32_t)be, len - 1, 1, got) == 0, "b.dat last byte ok");
        CHECK(got[0] == want[len - 1], "b.dat last byte value");
        CHECK(res_pack_read_range(pack, (uint32_t)be, len, 0, got) == 0, "b.dat off==size len 0 is 0");
        CHECK(res_pack_read_range(pack, (uint32_t)be, len, 1, got) == RES_RANGE_EOF, "b.dat at EOF is EOF");
        CHECK(res_pack_read_range(pack, (uint32_t)be, len - 10, 40, got) == RES_RANGE_EOF, "b.dat straddle EOF is EOF");
        free(want);
    }

    // A path that is not in the pack is not found.
    CHECK(res_pack_find(pack, "not/here.bin", 12) == -1, "absent path is -1");
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    scratch_anchor_to_exe();
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)logAlive;
    printf("anopak: mount, lookup, ranged read, deterministic build\n");

    CHECK(ano_res_init() == 0, "ano_res_init");

    // Build from the corpus.
    stage_corpus("respack_src", 0);
    CHECK(res_pack_build("respack_src", "respack_a.anopak", RES_CODEC_LZ4) == 0, "build a");

    // Determinism: a rebuild of the same tree is byte-identical.
    CHECK(res_pack_build("respack_src", "respack_a2.anopak", RES_CODEC_LZ4) == 0, "build a2");

    // Determinism: a build from a second tree staged in reverse order is byte-identical too.
    stage_corpus("respack_src2", 1);
    CHECK(res_pack_build("respack_src2", "respack_b.anopak", RES_CODEC_LZ4) == 0, "build b");

    {
        size_t na = 0, na2 = 0, nb = 0;
        uint8_t *a  = load_file("respack_a.anopak",  &na);
        uint8_t *a2 = load_file("respack_a2.anopak", &na2);
        uint8_t *b  = load_file("respack_b.anopak",  &nb);
        CHECK(a && a2 && b, "load built packs");
        CHECK(a && a2 && na == na2 && memcmp(a, a2, na) == 0, "rebuild is byte-identical");
        CHECK(a && b && na == nb && memcmp(a, b, na) == 0, "reordered build is byte-identical");
        free(a); free(a2); free(b);
    }

    // Self-exclusion: building INTO the source tree must not embed the previous archive --
    // the same command twice stays byte-identical, and both match the out-of-tree build.
    CHECK(res_pack_build("respack_src", "respack_src/self.anopak", RES_CODEC_LZ4) == 0,
          "build into the source tree");
    CHECK(res_pack_build("respack_src", "respack_src/self.anopak", RES_CODEC_LZ4) == 0,
          "rebuild into the source tree");
    {
        size_t ns = 0, na = 0;
        uint8_t *s = load_file("respack_src/self.anopak", &ns);
        uint8_t *a = load_file("respack_a.anopak", &na);
        CHECK(s && a && ns == na && memcmp(s, a, ns) == 0,
              "in-tree build byte-identical to out-of-tree");
        free(s); free(a);
    }
    remove("respack_src/self.anopak");

    // Mount and verify every entry.
    CHECK(res_pack_mount("", fspath("respack_a.anopak")) == 0, "mount a");
    CHECK(res_pack_count() == 1, "one pack mounted");
    const res_pack *pack = res_pack_at(0);
    CHECK(pack != NULL, "pack_at(0)");
    if (pack)
        verify_entries(pack);
    res_pack_unmount_all();
    CHECK(res_pack_count() == 0, "unmounted");

    // ------------------------------------------------------------------------------------
    // Corruption battery. Load the good pack, flip a single bit in each region, and confirm
    // the refusal. Failed mounts do not increment the mount count, so they need no unmount.
    size_t n = 0;
    uint8_t *base = load_file("respack_a.anopak", &n);
    CHECK(base != NULL && n > 48, "load pack for corruption");
    if (base != NULL && n > 48) {
        uint32_t ecount = rd32(base + 8);
        uint64_t toc_off = rd64(base + 16);
        uint64_t toc_bytes = (uint64_t)ecount * 48u;
        uint8_t *c = malloc(n);

        // 1. Magic bit.
        memcpy(c, base, n); c[0] ^= 0x01;
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "flipped magic refused");

        // 2. Hashed header region (a byte of toc_off, inside [0,24)).
        memcpy(c, base, n); c[20] ^= 0x01;
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "flipped header refused");

        // 3. TOC body (tag byte of entry 0).
        memcpy(c, base, n); c[toc_off + 40] ^= 0x01;
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "flipped TOC refused");

        // 4. The TOC hash itself.
        memcpy(c, base, n); c[toc_off + toc_bytes] ^= 0x01;
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "flipped TOC-hash refused");

        // 5. A payload bit: find the first non-empty entry, flip its first stored chunk byte.
        //    Mount succeeds (mount never reads payload); the read refuses via the chunk hash.
        uint64_t victim_off = 0; uint32_t victim_entry = 0; int found = 0;
        for (uint32_t i = 0; i < ecount; i++) {
            const uint8_t *te = base + toc_off + (uint64_t)i * 48u;
            uint64_t data_off = rd64(te + 16);
            uint64_t raw_size = rd64(te + 24);
            if (raw_size > 0) {
                uint64_t nchunks = (raw_size + RES_CODEC_CHUNK - 1) / RES_CODEC_CHUNK;
                victim_off = data_off + nchunks * 16u;  // first stored chunk byte
                victim_entry = i;
                found = 1;
                break;
            }
        }
        CHECK(found && victim_off < n, "locate a payload byte");
        if (found && victim_off < n) {
            memcpy(c, base, n); c[victim_off] ^= 0x01;
            write_file("respack_c.anopak", c, n);
            CHECK(res_pack_mount("", fspath("respack_c.anopak")) == 0, "payload-corrupt pack still mounts");
            const res_pack *cp = res_pack_at(res_pack_count() - 1);
            size_t rlen = (size_t)rd64(base + toc_off + (uint64_t)victim_entry * 48u + 24);
            uint8_t *tmp = malloc(rlen);
            CHECK(cp && res_pack_read_range(cp, victim_entry, 0, rlen, tmp) == -1,
                  "corrupt payload refused on read");
            free(tmp);
            res_pack_unmount_all();
        }

        // 6. A TOC forged to name GDEFLATE, with a recomputed TOC hash so the hash passes and
        //    the codec-availability refusal is what fires.
        memcpy(c, base, n);
        c[toc_off + 44] = (uint8_t)RES_CODEC_GDEFLATE;              // entry 0 codec byte
        wr64(c + toc_off + toc_bytes, fnv_oracle(c + toc_off, (size_t)toc_bytes));
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "forged GDEFLATE codec refused");

        // 7. A forged raw_size at the chunk_count_of overflow edge, TOC hash recomputed: the
        //    wrap guard is what fires, never a wrong chunk count trusted downstream.
        uint64_t ve = toc_off + (uint64_t)victim_entry * 48u;
        memcpy(c, base, n);
        wr64(c + ve + 24, UINT64_MAX);
        wr64(c + toc_off + toc_bytes, fnv_oracle(c + toc_off, (size_t)toc_bytes));
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "wrapping raw_size refused");

        // 8. A forged data_off near UINT64_MAX: data_off + stored_size wraps, and with it
        //    every in-entry offset the chunk walker would ever compute. Refused at mount.
        memcpy(c, base, n);
        wr64(c + ve + 16, UINT64_MAX - 3u);
        wr64(c + toc_off + toc_bytes, fnv_oracle(c + toc_off, (size_t)toc_bytes));
        write_file("respack_c.anopak", c, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == -1, "wrapping data_off refused");

        // 9. The file is REPLACED between mount and read: the per-read TOC-generation check
        //    refuses instead of serving the new file's chunk records under the old table.
        write_file("respack_c.anopak", base, n);
        CHECK(res_pack_mount("", fspath("respack_c.anopak")) == 0, "genuine pack mounts");
        {
            memcpy(c, base, n);
            c[toc_off + 40] ^= 0x01;                    // a different TOC generation
            wr64(c + toc_off + toc_bytes, fnv_oracle(c + toc_off, (size_t)toc_bytes));
            write_file("respack_c.anopak", c, n);
            const res_pack *rp = res_pack_at(res_pack_count() - 1);
            uint8_t tmp[64];
            int se = rp ? res_pack_find(rp, "a.txt", 5) : -1;
            CHECK(se >= 0, "find under the mounted table");
            CHECK(rp && se >= 0 && res_pack_read_range(rp, (uint32_t)se, 0, 13, tmp) == -1,
                  "swapped pack refused underfoot");
            res_pack_unmount_all();
        }

        free(c);
        free(base);
        remove("respack_c.anopak");
    }

    // Cleanup.
    unstage_corpus("respack_src");
    unstage_corpus("respack_src2");
    remove("respack_a.anopak");
    remove("respack_a2.anopak");
    remove("respack_b.anopak");

    CHECK(ano_res_shutdown() == 0, "resource shutdown");

    if (failures == 0) { printf("anotest_respack: all checks passed\n"); return 0; }
    printf("anotest_respack: %d check(s) failed\n", failures);
    return 1;
}
