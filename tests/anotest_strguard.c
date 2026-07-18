/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: collation of cased letters whose decomposition targets were trimmed. ce_push_rune
// consults the decomp table before the CE table (docs/BUGS.md, Strings / Implementation,
// ano_strings_collate.c:75), and the generated tables disagree about coverage: U+01EF (ǯ,
// ezh-caron, Skolt Sami) and U+0374 (Greek numeral sign) carry correct direct CE listings
// ([270B.020.02]+[0000.028.02], [04F2.020.02]) that are unreachable because their decomp
// entries redirect through U+0292 / U+02B9, both absent from the trimmed CE table, so
// ce_push_cp falls to UCA implicit weights (primary 0xFBC0, after every listed script). The
// uppercase sibling U+01EE decomposes through U+01B7, which the trim kept -- so the case pair
// the module itself maps (anorune_to_lower(U+01EE) == U+01EF) splits across the whole
// collation space: Ǯ sorts in the Latin band, ǯ after kana with the Han implicits, and
// anostr_eq_base calls them unequal. Controls pin a healthy decomposing case pair (Ǒ/ǒ, both
// targets listed) and the case-table mapping, so a fix cannot pass by weakening either.
// Headless, deterministic. Exit 0 == pass.

#include <stdio.h>

#include <anoptic_strings_utf.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    anostr_t EZH_UP  = anostr_lit("\xC7\xAE");      // U+01EE Ǯ
    anostr_t ezh_lo  = anostr_lit("\xC7\xAF");      // U+01EF ǯ
    anostr_t OCARON  = anostr_lit("\xC7\x91");      // U+01D1 Ǒ
    anostr_t ocaron  = anostr_lit("\xC7\x92");      // U+01D2 ǒ
    anostr_t hira_a  = anostr_lit("\xE3\x81\x82");  // U+3042 あ
    anostr_t greekns = anostr_lit("\xCD\xB4");      // U+0374 Greek numeral sign
    anostr_t alpha   = anostr_lit("\xCE\xB1");      // U+03B1 α

    // control: the case table maps the pair both ways (the letter is in-scope for the module)
    CHECK(anorune_to_lower(0x01EE) == 0x01EF, "case table lowers U+01EE to U+01EF");
    CHECK(anorune_to_upper(0x01EF) == 0x01EE, "case table uppers U+01EF to U+01EE");

    // control: a decomposing case pair whose targets survived the trim behaves (o-caron)
    CHECK(anostr_eq_base(OCARON, ocaron), "eq_base holds for the o-caron case pair");
    CHECK(anostr_collate(ocaron, OCARON) < 0, "o-caron case pair differs only at level three");
    CHECK(anostr_collate(OCARON, hira_a) < 0 && anostr_collate(ocaron, hira_a) < 0,
          "both o-caron forms sort in the Latin band, before kana");

    // bug probe: the ezh-caron case pair must behave the same way
    CHECK(anostr_eq_base(EZH_UP, ezh_lo), "eq_base holds for the ezh-caron case pair");
    CHECK(anostr_collate(ezh_lo, EZH_UP) < 0, "ezh-caron case pair differs only at level three");
    CHECK(anostr_collate(ezh_lo, hira_a) < 0, "lowercase ezh-caron sorts in the Latin band, before kana");

    // bug probe: sort keeps the case pair adjacent in the Latin band, kana last
    anostr_t items[4] = { ezh_lo, anostr_lit("z"), EZH_UP, hira_a };
    anostr_sort(items, 4);
    CHECK(anostr_eq(items[0], anostr_lit("z")), "z sorts first");
    CHECK(anostr_eq(items[1], ezh_lo) && anostr_eq(items[2], EZH_UP),
          "ezh-caron case pair sits together after z");
    CHECK(anostr_eq(items[3], hira_a), "kana sorts last, after every Latin letter");

    // bug probe: U+0374's direct CE places it with the symbols, before Greek letters
    CHECK(anostr_collate(greekns, alpha) < 0, "Greek numeral sign sorts before alpha");

    if (failures) {
        printf("anotest_strguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_strguard: all passed\n");
    return 0;
}
