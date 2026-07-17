/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_text_window_sum's unconditional first-point fetch (text_raster_ref.c:92-:93):
// pts[g->pointOffset] is read BEFORE the curveCount loop that owns the stream walk, and the
// contract carries no curveCount > 0 precondition (text_internal.h:96 "Unclamped coverage sum
// ... Pure, any thread"; docs/text/font-render.md:444 drives it directly as the offline
// harness). The bake mints exactly the entry that breaks the fetch: a zero-curve glyph 〜
// MISSING codepoint or blank outline, both documented output (anoptic_text.h:82 "Missing ->
// blank", :48 "0 = blank") 〜 keeps pointOffset = stream.count as assigned before load
// (text_bake.c:565) with nothing pushed after, so a zero-curve TAIL glyph has
// pointOffset == pointCount and the fetch reads one uint32 past the points blob, and an
// all-blank bake (stream empty, points NULL 〜 text_bake.c:639) NULL-derefs outright. The
// fetched value is discarded (the loop never runs, the sum is 0.0 regardless), so the defect
// is the read itself. Every in-tree consumer compensates with its own curveCount guard
// (text_raster_ref.c:122, text_shape.c:98, textcoverage.glsl:142, textraster.comp:55,
// textworld.vert:42) 〜 the unanimity that keeps the hole latent for the next direct caller.
// (docs/BUGS.md, Text / Interface-level, text_raster_ref.c:92).
// Harness: real FreeType Geist bake 〜 'A' plus a plane-16 PUA codepoint no face maps, so the
// bake itself mints the MISSING zero-curve tail. The stream bytes are the bake's own; they are
// relocated flush against a guard page only so the abstract-machine over-read is a
// deterministic crash instead of a silent heap read. A second bake of ' ' alone produces the
// all-blank NULL-stream shape with no relocation at all.
// CONTROL A: 'A' bbox coverage over the heap stream is positive 〜 a reject-everything fix
// cannot pass. CONTROL B: the same sum over the relocated stream is bit-identical, proving
// every legitimate read stays in bounds right up to the page edge. CONTROL C: a zero-curve
// entry whose stray fetch lands mid-stream sums 0.0 〜 the fetched word never matters.
// TRIGGER 1: window_sum on the bake's own MISSING tail entry over the relocated stream 〜
// today the first fetch lands on the guard page. TRIGGER 2: window_sum over the space-only
// bake's NULL stream. Both must simply sum 0.0 once the fetch honors curveCount == 0; either
// fix shape (read-site guard or a bake layout that keeps blank offsets in-stream) passes.
// A crash is a valid failure signal. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "anoptic_filesystem.h"
#include "anoptic_memory.h"
#include "anoptic_text.h"
#include "text/text_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define FONT_PATH "resources/fonts/Geist/static/Geist-Regular.ttf"

// One RW page followed by one inaccessible page. Returns the RW page base and its size via
// *pageOut, or NULL on failure. Never freed 〜 process-lifetime harness memory.
static uint8_t *guard_page(size_t *pageOut)
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size_t page = si.dwPageSize;
    uint8_t *base = VirtualAlloc(NULL, page * 2u, MEM_RESERVE, PAGE_NOACCESS);
    if (base == NULL || VirtualAlloc(base, page, MEM_COMMIT, PAGE_READWRITE) == NULL)
        return NULL;
#else
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *base = mmap(NULL, page * 2u, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED || mprotect(base + page, page, PROT_NONE) != 0)
        return NULL;
#endif
    *pageOut = page;
    return base;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    CHECK(ano_fs_chdir_gamepath(), "chdir to the exe directory (staged font root)");
    CHECK(ano_text_init() == 0, "text module init");
    AnoFontId geist = ano_text_font_load_lit(FONT_PATH);
    CHECK(geist != 0, "Geist loads");
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    CHECK(heap != NULL, "bake heap");
    if (failures)
        return 1;

    // Mixed bake: 'A', then a plane-16 PUA codepoint no face maps -> MISSING zero-curve tail.
    const AnoBakeRange ranges[2] = {
        { .font = geist, .first = 'A', .last = 'A' },
        { .font = geist, .first = 0x10FFFDu, .last = 0x10FFFDu },
    };
    AnoFontBake mixed;
    CHECK(ano_text_font_bake_ranges(ranges, 2u, heap, &mixed) == 0, "mixed bake succeeds");
    if (failures)
        return 1;
    const AnoGlyphEntry *gA   = &mixed.glyphs[0];
    const AnoGlyphEntry *tail = &mixed.glyphs[1];
    CHECK(mixed.glyphCount == 2u && mixed.pointCount > 0u, "mixed bake: two slots, live stream");
    CHECK(gA->curveCount > 0u, "'A' has ink");
    CHECK((tail->flags & ANO_GLYPH_MISSING) != 0u, "PUA tail is MISSING (anoptic_text.h:82)");
    CHECK(tail->curveCount == 0u, "MISSING tail is blank (anoptic_text.h:48)");
    printf("layout: tail pointOffset=%u pointCount=%u (equal today: fetch is one past the stream)\n",
           tail->pointOffset, mixed.pointCount);

    // All-blank bake: ' ' alone loads fine, has no contours, and the stream stays empty.
    AnoFontBake blank;
    CHECK(ano_text_font_bake(geist, ' ', ' ', heap, &blank) == 0, "space-only bake succeeds");
    if (failures)
        return 1;
    CHECK(blank.glyphCount == 1u && blank.glyphs[0].curveCount == 0u, "space is blank");
    printf("layout: space-only bake points=%p pointCount=%u (NULL today: fetch is a NULL deref)\n",
           (const void *)blank.points, blank.pointCount);

    // control A: positive coverage over 'A's bbox from the heap stream.
    float bw = gA->bboxMax[0] - gA->bboxMin[0];
    float bh = gA->bboxMax[1] - gA->bboxMin[1];
    float sumHeap = ano_text_window_sum(mixed.points, gA, gA->bboxMin[0], gA->bboxMin[1], bw, bh);
    printf("control A: 'A' bbox coverage over heap stream = %f\n", (double)sumHeap);
    CHECK(sumHeap > 0.01f && sumHeap < 1.2f, "control A: 'A' bbox coverage positive and sane");

    // control B: the bake's own bytes relocated flush against the guard page reproduce 'A'
    // bit-exactly 〜 every legitimate read is in bounds right up to the page edge.
    size_t page = 0;
    uint8_t *base = guard_page(&page);
    CHECK(base != NULL, "guard page maps");
    size_t bytes = (size_t)mixed.pointCount * sizeof(uint32_t);
    CHECK(bytes <= page, "stream fits one page");
    if (failures)
        return 1;
    uint32_t *rel = (uint32_t *)(base + page - bytes);
    memcpy(rel, mixed.points, bytes);
    float sumRel = ano_text_window_sum(rel, gA, gA->bboxMin[0], gA->bboxMin[1], bw, bh);
    CHECK(sumRel == sumHeap, "control B: relocated stream reproduces 'A' bit-exactly");

    // control C: a zero-curve entry whose stray fetch lands mid-stream sums 0.0 today and
    // after any fix 〜 the fetched word never matters.
    AnoGlyphEntry interior = *tail;
    interior.pointOffset = 0u;
    float sumInterior = ano_text_window_sum(rel, &interior, 0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(sumInterior == 0.0f, "control C: zero-curve glyph mid-stream sums 0.0");

    // trigger 1: the bake's own MISSING tail entry over the relocated stream. Today
    // pointOffset == pointCount and the first fetch reads the guard page 〜 access violation.
    printf("trigger 1: window_sum on the MISSING tail glyph (fetch at word %u of a %u-word stream)\n",
           tail->pointOffset, mixed.pointCount);
    float sumTail = ano_text_window_sum(rel, tail, 0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(sumTail == 0.0f, "trigger 1: blank tail glyph sums 0.0 without leaving its empty extent");

    // trigger 2: the all-blank bake exactly as minted 〜 no relocation, points is NULL today.
    printf("trigger 2: window_sum over the space-only bake's stream\n");
    float sumBlank = ano_text_window_sum(blank.points, &blank.glyphs[0], 0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(sumBlank == 0.0f, "trigger 2: all-blank bake sums 0.0 without touching a NULL stream");

    ano_text_shutdown();
    if (failures) {
        printf("anotest_textblanksumguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_textblanksumguard: all passed\n");
    return 0;
}
