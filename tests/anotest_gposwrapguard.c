/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_gpos_extract_kerns' subtable-offset arithmetic (text_gpos.c:304 so += innerOff,
// sibling class-matrix wrap at :186 (c1*c2n+c2)*rec). Both are unchecked uint32 offset math. A
// type-9 Extension subtable whose 32-bit extensionOffset is crafted so so += innerOff overflows
// uint32 lands `so` back INSIDE the buffer at a different, valid-parsing structure; every g16/g32
// read then stays in bounds, so the malformation is never detected and the function returns 0
// (success) instead of the nonzero its header contract promises for malformed input
// (text_internal.h:89: "Bounds-checked. Malformed -> nonzero with dense possibly partial. 0 =
// success including 'no kerns'."). A caller cannot tell a corrupt/hostile GPOS table apart from a
// well-formed one with no kerns 〜 the false arm it needs to fall back never fires.
// (docs/BUGS.md, Text / Interface-level, text_gpos.c:304).
// Harness: links anoptic_core (text_gpos.c lives there); builds raw GPOS byte tables by hand and
// drives the public extractor directly. No FreeType, no device.
// CONTROL A: a well-formed type-2 PairPos table with one kern pair (gid 3 -> gid 5, -30) 〜 must
// return 0 and write dense[0*2+1] == -30, so a reject-everything fix cannot pass.
// CONTROL B: a well-formed type-9 Extension PairPos (small forward extensionOffset) with the same
// kern 〜 must also return 0 with dense[1] == -30, so a reject-all-Extension fix cannot pass.
// TRIGGER:  the same scaffold, but the Extension's extensionOffset is 0xFFFFFFE2 so so (92) wraps
// to 62, a decoy empty-coverage PairPos; the table is malformed yet today returns 0. Per the
// header contract the return must be nonzero; CHECK(ret != 0) fails today.
// A crash is a valid failure signal. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "text/text_internal.h" // ano_gpos_extract_kerns (text_internal.h:90)

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Big-endian scalar writers into a raw GPOS byte table */

static void put16(uint8_t *b, uint32_t off, uint16_t v)
{
    b[off] = (uint8_t)(v >> 8);
    b[off + 1u] = (uint8_t)v;
}

static void put32(uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t)(v >> 24);
    b[off + 1u] = (uint8_t)(v >> 16);
    b[off + 2u] = (uint8_t)(v >> 8);
    b[off + 3u] = (uint8_t)v;
}


/* Shared header -> LookupList reach, identical for all three tables. Lookup sits at 84. */

#define TAG_LATN 0x6C61746Eu
#define TAG_KERN 0x6B65726Eu
#define GPOS_LEN 256u

// Writes GPOS header, ScriptList/Script/LangSys (latn default), FeatureList/Feature (kern ->
// lookup 0), LookupList (lookup 0 -> offset 84). Offsets 0..83; the caller writes the lookup.
static void build_scaffold(uint8_t *b)
{
    memset(b, 0, GPOS_LEN);
    // GPOS header
    put16(b, 0u, 1u);      // majorVersion
    put16(b, 2u, 0u);      // minorVersion
    put16(b, 4u, 16u);     // scriptListOffset
    put16(b, 6u, 48u);     // featureListOffset
    put16(b, 8u, 80u);     // lookupListOffset
    // ScriptList @16
    put16(b, 16u, 1u);     // scriptCount
    put32(b, 18u, TAG_LATN);
    put16(b, 22u, 8u);     // scriptOffset (from ScriptList) -> Script @24
    // Script @24
    put16(b, 24u, 8u);     // defaultLangSysOffset (from Script) -> LangSys @32
    // LangSys @32
    put16(b, 32u, 0u);     // lookupOrderOffset
    put16(b, 34u, 0xFFFFu);// requiredFeatureIndex (none)
    put16(b, 36u, 1u);     // featureIndexCount
    put16(b, 38u, 0u);     // featureIndices[0] = feature 0
    // FeatureList @48
    put16(b, 48u, 1u);     // featureCount
    put32(b, 50u, TAG_KERN);
    put16(b, 54u, 8u);     // featureOffset (from FeatureList) -> Feature @56
    // Feature @56
    put16(b, 56u, 0u);     // featureParamsOffset
    put16(b, 58u, 1u);     // lookupIndexCount
    put16(b, 60u, 0u);     // lookupListIndices[0] = lookup 0
    // LookupList @80
    put16(b, 80u, 1u);     // lookupCount
    put16(b, 82u, 4u);     // lookupOffsets[0] (from LookupList) -> Lookup @84
}

// One PairPos format 1 covering a single glyph gid1 with a single pair (gid2, xAdvance). Written
// at `at`; needs 24 bytes of room. Layout: header 10, pairSetOffset 2, coverage 6, pairSet 6.
static void put_pairpos_f1(uint8_t *b, uint32_t at, uint16_t gid1, uint16_t gid2, int16_t xadv)
{
    put16(b, at + 0u, 1u);        // posFormat
    put16(b, at + 2u, 12u);       // coverageOffset -> at+12
    put16(b, at + 4u, 0x0004u);   // valueFormat1 = xAdvance
    put16(b, at + 6u, 0x0000u);   // valueFormat2
    put16(b, at + 8u, 1u);        // pairSetCount
    put16(b, at + 10u, 18u);      // pairSetOffsets[0] -> at+18
    // Coverage format 1 @ at+12
    put16(b, at + 12u, 1u);       // coverageFormat
    put16(b, at + 14u, 1u);       // glyphCount
    put16(b, at + 16u, gid1);     // glyphArray[0]
    // PairSet @ at+18
    put16(b, at + 18u, 1u);       // pairValueCount
    put16(b, at + 20u, gid2);     // secondGlyph
    put16(b, at + 22u, (uint16_t)xadv); // valueRecord1.xAdvance
}


/* Table builders */

// CONTROL A: type-2 PairPos directly under the lookup.
static void build_control_a(uint8_t *b)
{
    build_scaffold(b);
    put16(b, 84u, 2u);  // lookupType = PairPos
    put16(b, 86u, 0u);  // lookupFlag
    put16(b, 88u, 1u);  // subTableCount
    put16(b, 90u, 8u);  // subtableOffsets[0] (from Lookup) -> PairPos @92
    put_pairpos_f1(b, 92u, 3u, 5u, -30);
}

// CONTROL B: type-9 Extension with a small forward extensionOffset -> a real PairPos @108.
static void build_control_b(uint8_t *b)
{
    build_scaffold(b);
    put16(b, 84u, 9u);  // lookupType = Extension
    put16(b, 86u, 0u);  // lookupFlag
    put16(b, 88u, 1u);  // subTableCount
    put16(b, 90u, 8u);  // subtableOffsets[0] (from Lookup) -> Extension @92
    // ExtensionPosFormat1 @92
    put16(b, 92u, 1u);  // posFormat
    put16(b, 94u, 2u);  // extensionLookupType = PairPos
    put32(b, 96u, 16u); // extensionOffset (from Extension) -> PairPos @108, no wrap
    put_pairpos_f1(b, 108u, 3u, 5u, -30);
}

// TRIGGER: type-9 Extension whose 32-bit extensionOffset wraps so (92) back to 62, an empty-
// coverage decoy PairPos. Malformed, yet parses in bounds and returns 0.
static void build_trigger(uint8_t *b)
{
    build_scaffold(b);
    put16(b, 84u, 9u);  // lookupType = Extension
    put16(b, 86u, 0u);  // lookupFlag
    put16(b, 88u, 1u);  // subTableCount
    put16(b, 90u, 8u);  // subtableOffsets[0] (from Lookup) -> Extension @92
    // ExtensionPosFormat1 @92
    put16(b, 92u, 1u);          // posFormat
    put16(b, 94u, 2u);          // extensionLookupType = PairPos
    put32(b, 96u, 0xFFFFFFE2u); // extensionOffset: 92 + 0xFFFFFFE2 wraps uint32 to 62
    // Decoy PairPos format 1 @62 with an empty coverage -> covers nothing, applies nothing.
    put16(b, 62u, 1u);   // posFormat
    put16(b, 64u, 10u);  // coverageOffset -> 72
    put16(b, 66u, 0u);   // valueFormat1
    put16(b, 68u, 0u);   // valueFormat2
    put16(b, 70u, 0u);   // pairSetCount
    // Coverage format 1 @72, zero glyphs
    put16(b, 72u, 1u);   // coverageFormat
    put16(b, 74u, 0u);   // glyphCount
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    static uint8_t table[GPOS_LEN];
    const uint32_t gids[2] = { 3u, 5u };
    int32_t dense[4];

    // control A: well-formed type-2 PairPos. Must parse and yield the kern, so a reject-all fix
    // cannot pass.
    printf("control A: well-formed type-2 PairPos\n");
    build_control_a(table);
    memset(dense, 0, sizeof dense);
    int ra = ano_gpos_extract_kerns(table, GPOS_LEN, gids, 2u, dense);
    printf("  ret=%d dense={%d,%d,%d,%d}\n", ra, dense[0], dense[1], dense[2], dense[3]);
    CHECK(ra == 0, "control A: well-formed table returns 0");
    CHECK(dense[1] == -30, "control A: kern gid3->gid5 accumulated as -30");
    CHECK(dense[0] == 0 && dense[2] == 0 && dense[3] == 0, "control A: no stray kerns");

    // control B: well-formed type-9 Extension (small forward offset). Same kern; proves a
    // reject-all-Extension fix cannot pass.
    printf("control B: well-formed type-9 Extension PairPos\n");
    build_control_b(table);
    memset(dense, 0, sizeof dense);
    int rb = ano_gpos_extract_kerns(table, GPOS_LEN, gids, 2u, dense);
    printf("  ret=%d dense={%d,%d,%d,%d}\n", rb, dense[0], dense[1], dense[2], dense[3]);
    CHECK(rb == 0, "control B: well-formed Extension returns 0");
    CHECK(dense[1] == -30, "control B: kern gid3->gid5 accumulated as -30");

    // trigger: Extension extensionOffset wraps so past uint32 back into the buffer. The table is
    // malformed; the contract (text_internal.h:89) demands a nonzero return. Today the wrap lands
    // in bounds, parses a decoy that covers nothing, and the extractor returns 0.
    printf("trigger: type-9 Extension extensionOffset 0xFFFFFFE2 wraps so 92 -> 62\n");
    build_trigger(table);
    memset(dense, 0, sizeof dense);
    int rt = ano_gpos_extract_kerns(table, GPOS_LEN, gids, 2u, dense);
    printf("  ret=%d dense={%d,%d,%d,%d}\n", rt, dense[0], dense[1], dense[2], dense[3]);
    CHECK(rt != 0, "trigger: malformed wrapping extensionOffset must return nonzero (text_gpos.c:304)");

    if (failures) {
        printf("anotest_gposwrapguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_gposwrapguard: all passed\n");
    return 0;
}
