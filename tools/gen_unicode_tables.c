/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Generates src/strings/ano_unicode_tables.h and src/strings/ano_collate_tables.h
// from the Unicode Character Database. Standalone offline tool, not part of the
// engine build. Output is committed, regenerated per Unicode version.
//
// Usage (from the repository root):
//     curl -o tools/ucd/ReadMe.txt      https://www.unicode.org/Public/UCD/latest/ucd/ReadMe.txt
//     curl -o tools/ucd/UnicodeData.txt https://www.unicode.org/Public/UCD/latest/ucd/UnicodeData.txt
//     curl -o tools/ucd/PropList.txt    https://www.unicode.org/Public/UCD/latest/ucd/PropList.txt
//     curl -o tools/ucd/allkeys.txt     https://www.unicode.org/Public/UCA/latest/allkeys.txt
//     gcc -O2 -o gen_unicode_tables tools/gen_unicode_tables.c
//     ./gen_unicode_tables
//
// Both tables are two-stage lookups covering every code point 0..0x10FFFF:
//     stage1[cp >> 8] -> block index -> stage2[block*256 + (cp & 0xFF)] -> record
// with identical records and identical 256-entry blocks deduplicated.
//
// ano_unicode_tables.h: simple case-mapping deltas (0 = uncased/identity) and flags
// (letter = category L*, digit = Nd, mark = M*, whitespace = the White_Space property).
//
// ano_collate_tables.h: DUCET collation elements packed primary(16).secondary(11).tertiary(5)
// per u32, plus fully-expanded canonical decompositions. Contractions are skipped and
// per-script @implicitweights are folded into one uniform runtime formula.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CP   0x110000   // parsed range (UCD data mentions the SMP and beyond)
#define TABLE_CP 0x10000    // emitted range: the keep-lists are BMP-only, runtime guards

#define FLAG_LETTER     1u
#define FLAG_DIGIT      2u
#define FLAG_WHITESPACE 4u
#define FLAG_MARK       8u
#define FLAG_PUNCT      16u

static uint8_t flags[MAX_CP];
static int32_t upper_delta[MAX_CP];
static int32_t lower_delta[MAX_CP];

// int32 deltas: rare pairs span blocks (U+019B uppercases into Latin Extended-D).
typedef struct record_t {
    int32_t upper_delta;
    int32_t lower_delta;
    uint8_t flags;
} record_t;

static record_t records[4096];
static size_t   record_count;
static uint16_t record_of_cp[MAX_CP];

#define BLOCK_COUNT (TABLE_CP / 256)
static uint16_t stage1[BLOCK_COUNT];
static uint16_t stage2[BLOCK_COUNT * 256];   // worst case: every block unique
static size_t   block_count;

// Collation: packed CEs, one span per listed code point. Record 0 = unlisted (implicit).
#define CE_MAX_PER_CP 20
typedef struct ce_span_t { uint32_t offset; uint8_t len; } ce_span_t;
static uint32_t  ce_pool[262144];
static size_t    ce_pool_count;
static ce_span_t ce_spans[49152];
static size_t    ce_span_count;
static uint16_t  ce_span_of_cp[MAX_CP];

// Canonical decompositions, fully expanded (recursive), in code point order.
typedef struct decomp_t { uint32_t cp; uint16_t offset; uint8_t len; } decomp_t;
#define DECOMP_MAX_LEN 8
static uint32_t decomp_pool[32768];
static size_t   decomp_pool_count;
static decomp_t decomps[8192];
static size_t   decomp_count;

static FILE *open_or_die(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f == NULL) {
        fprintf(stderr, "cannot open %s (see the usage comment for the curl commands)\n", path);
        exit(1);
    }
    return f;
}

// Split line on ';' in place. Preserves empty fields (unlike strtok). Returns field count.
static int split_fields(char *line, char *fields[], int max_fields)
{
    int n = 0;
    fields[n++] = line;
    for (char *p = line; *p != '\0' && n < max_fields; p++) {
        if (*p == ';') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

static int is_letter_category(const char *cat)
{
    return cat[0] == 'L' && (cat[1] == 'u' || cat[1] == 'l' || cat[1] == 't' ||
                             cat[1] == 'm' || cat[1] == 'o');
}

// Combining marks: Mn nonspacing, Mc spacing, Me enclosing.
static int is_mark_category(const char *cat)
{
    return cat[0] == 'M' && (cat[1] == 'n' || cat[1] == 'c' || cat[1] == 'e');
}

// Punctuation: P* only. Symbols (S*) are not punctuation.
static int is_punct_category(const char *cat)
{
    return cat[0] == 'P' && (cat[1] == 'c' || cat[1] == 'd' || cat[1] == 's' ||
                             cat[1] == 'e' || cat[1] == 'i' || cat[1] == 'f' ||
                             cat[1] == 'o');
}

static uint8_t category_flags(const char *cat)
{
    if (is_letter_category(cat)) return FLAG_LETTER;
    if (strcmp(cat, "Nd") == 0)  return FLAG_DIGIT;
    if (is_mark_category(cat))   return FLAG_MARK;
    if (is_punct_category(cat))  return FLAG_PUNCT;
    return 0;
}

static void parse_unicode_data(void)
{
    FILE *f = open_or_die("tools/ucd/UnicodeData.txt", "r");
    char line[1024];
    long range_start = -1;
    char range_cat[4] = {0};

    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *fields[16];
        if (split_fields(line, fields, 16) < 14)
            continue;
        long cp = strtol(fields[0], NULL, 16);
        const char *name = fields[1];
        const char *cat = fields[2];

        // <CJK Ideograph, First> / <..., Last> pairs describe whole ranges.
        size_t namelen = strlen(name);
        if (namelen > 8 && strcmp(name + namelen - 8, ", First>") == 0) {
            range_start = cp;
            strncpy(range_cat, cat, 3);
            continue;
        }
        if (namelen > 7 && strcmp(name + namelen - 7, ", Last>") == 0) {
            uint8_t fl = category_flags(range_cat);
            for (long c = range_start; c <= cp; c++)
                flags[c] |= fl;
            range_start = -1;
            continue;
        }

        flags[cp] |= category_flags(cat);
        if (fields[12][0] != '\0')
            upper_delta[cp] = (int32_t)(strtol(fields[12], NULL, 16) - cp);
        if (fields[13][0] != '\0')
            lower_delta[cp] = (int32_t)(strtol(fields[13], NULL, 16) - cp);

        // Field 5: canonical decomposition ("<...>"-tagged ones are compatibility, skipped).
        if (fields[5][0] != '\0' && fields[5][0] != '<') {
            decomp_t d = { (uint32_t)cp, (uint16_t)decomp_pool_count, 0 };
            char *p = fields[5];
            while (*p != '\0') {
                char *end;
                unsigned long piece = strtoul(p, &end, 16);
                if (end == p)
                    break;
                decomp_pool[decomp_pool_count++] = (uint32_t)piece;
                d.len++;
                p = end;
            }
            decomps[decomp_count++] = d;
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------------------------------------
// Decomposition expansion: UnicodeData's one-level decompositions expanded to full NFD.
// Input is in cp order, bsearched.

static const decomp_t *find_decomp(uint32_t cp)
{
    size_t lo = 0, hi = decomp_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (decomps[mid].cp < cp) lo = mid + 1;
        else hi = mid;
    }
    return lo < decomp_count && decomps[lo].cp == cp ? &decomps[lo] : NULL;
}

static int expand_cp(uint32_t cp, uint32_t *out, int cap)
{
    const decomp_t *d = find_decomp(cp);
    if (d == NULL) {
        if (cap < 1) { fprintf(stderr, "decomposition overflow\n"); exit(1); }
        out[0] = cp;
        return 1;
    }
    int n = 0;
    for (int k = 0; k < d->len; k++)
        n += expand_cp(decomp_pool[d->offset + k], out + n, cap - n);
    return n;
}

static int collate_kept(uint32_t cp);

// Expand recursively, keeping only entries in the collation keep-list.
static void expand_decompositions(void)
{
    static uint32_t pool2[sizeof decomp_pool / sizeof decomp_pool[0]];
    static decomp_t recs2[sizeof decomps / sizeof decomps[0]];
    size_t pool2_count = 0, kept = 0;
    for (size_t k = 0; k < decomp_count; k++) {
        if (!collate_kept(decomps[k].cp))
            continue;
        uint32_t full[DECOMP_MAX_LEN];
        int n = expand_cp(decomps[k].cp, full, DECOMP_MAX_LEN);
        recs2[kept++] = (decomp_t){ decomps[k].cp, (uint16_t)pool2_count, (uint8_t)n };
        memcpy(&pool2[pool2_count], full, (size_t)n * sizeof full[0]);
        pool2_count += (size_t)n;
    }
    memcpy(decomp_pool, pool2, pool2_count * sizeof pool2[0]);
    memcpy(decomps, recs2, kept * sizeof recs2[0]);
    decomp_pool_count = pool2_count;
    decomp_count = kept;
}

// ---------------------------------------------------------------------------------------------
// DUCET. Single code points only: contractions skipped, @implicitweights ranges fall
// through to the runtime's uniform implicit formula.

// Collation keep-list: the scripts the game ships. Everything outside falls back to
// implicit weights, code point order (= UTF-8 byte order), after all listed scripts.
// Han is implicit (no alphabet, cp order ~ radical-stroke).
typedef struct range_t { uint32_t lo, hi; } range_t;
static const range_t collate_keep[] = {
    { 0x0000, 0x024F },     // Basic Latin .. Latin Extended-B (ASCII, all of Latin Europe)
    { 0x0300, 0x036F },     // combining marks (decomposed accents carry the secondaries)
    { 0x0370, 0x03FF },     // Greek and Coptic
    { 0x0400, 0x052F },     // Cyrillic + Supplement
    { 0x16A0, 0x16FF },     // Runic (Futhark)
    { 0x1E00, 0x1EFF },     // Latin Extended Additional (Vietnamese, Welsh)
    { 0x1F00, 0x1FFF },     // Greek Extended (polytonic / Ancient Greek)
    { 0x2000, 0x206F },     // General Punctuation (real quotes, dashes)
    { 0x20A0, 0x20CF },     // currency signs
    { 0x3000, 0x30FF },     // CJK punctuation, hiragana, katakana
    { 0x31F0, 0x31FF },     // katakana phonetic extensions
    { 0xFF00, 0xFFEF },     // half/fullwidth forms
};

static int collate_kept(uint32_t cp)
{
    for (size_t k = 0; k < sizeof collate_keep / sizeof collate_keep[0]; k++)
        if (cp >= collate_keep[k].lo && cp <= collate_keep[k].hi)
            return 1;
    return 0;
}

// Classification/case keep-list: the collation scripts plus Han.
static const range_t class_extra[] = {
    { 0x3400, 0x4DBF },     // CJK Extension A
    { 0x4E00, 0x9FFF },     // CJK Unified Ideographs
};

static int class_kept(uint32_t cp)
{
    if (collate_kept(cp))
        return 1;
    for (size_t k = 0; k < sizeof class_extra / sizeof class_extra[0]; k++)
        if (cp >= class_extra[k].lo && cp <= class_extra[k].hi)
            return 1;
    return 0;
}

static void parse_allkeys(void)
{
    FILE *f = open_or_die("tools/ucd/allkeys.txt", "r");
    char line[2048];
    ce_span_count = 1;      // span 0 = not listed, runtime computes implicit weights

    while (fgets(line, sizeof line, f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        if (line[0] == '@' || line[0] == '\0')
            continue;
        char *semi = strchr(line, ';');
        if (semi == NULL)
            continue;
        *semi = '\0';

        // Code points before the ';'. More than one = a contraction, skipped.
        uint32_t cp = 0;
        int ncp = 0;
        for (char *p = line; ; ) {
            while (*p == ' ' || *p == '\t') p++;
            char *end;
            unsigned long v = strtoul(p, &end, 16);
            if (end == p)
                break;
            cp = (uint32_t)v;
            ncp++;
            p = end;
        }
        if (ncp != 1 || !collate_kept(cp))
            continue;

        // Collation elements: [.pppp.ssss.tttt] or [*pppp.ssss.tttt], one or more.
        uint32_t seq[CE_MAX_PER_CP];
        uint8_t  seq_len = 0;
        for (char *q = semi + 1; (q = strchr(q, '[')) != NULL; ) {
            q++;
            if (*q == '.' || *q == '*')
                q++;
            unsigned p1, s, t;
            if (sscanf(q, "%x.%x.%x", &p1, &s, &t) != 3)
                break;
            if (p1 > 0xFFFFu || s > 0x7FFu || t > 0x1Fu) {
                fprintf(stderr, "weight out of packing range at U+%04X\n", cp);
                exit(1);
            }
            if (seq_len >= CE_MAX_PER_CP) {
                fprintf(stderr, "too many CEs at U+%04X\n", cp);
                exit(1);
            }
            seq[seq_len++] = (uint32_t)p1 << 16 | (uint32_t)s << 5 | (uint32_t)t;
        }
        if (seq_len == 0)
            continue;

        // Reuse any identical run already in the pool.
        ce_span_t span = { UINT32_MAX, seq_len };
        for (size_t at = 0; at + seq_len <= ce_pool_count; at++) {
            if (memcmp(&ce_pool[at], seq, seq_len * sizeof seq[0]) == 0) {
                span.offset = (uint32_t)at;
                break;
            }
        }
        if (span.offset == UINT32_MAX) {
            span.offset = (uint32_t)ce_pool_count;
            memcpy(&ce_pool[ce_pool_count], seq, seq_len * sizeof seq[0]);
            ce_pool_count += seq_len;
        }
        if (ce_span_count >= sizeof ce_spans / sizeof ce_spans[0] ||
            ce_pool_count >= sizeof ce_pool / sizeof ce_pool[0]) {
            fprintf(stderr, "collation table overflow\n");
            exit(1);
        }
        ce_spans[ce_span_count] = span;
        ce_span_of_cp[cp] = (uint16_t)ce_span_count++;
    }
    fclose(f);
}

// The runtime CE queue holds one source rune's worth of CEs. Bound it here:
// decomposed pieces contribute their span lengths (or 2 for implicit weights).
static void assert_ce_queue_bound(int runtime_cap)
{
    for (long cp = 0; cp < MAX_CP; cp++) {
        const decomp_t *d = find_decomp((uint32_t)cp);
        int total = 0;
        if (d == NULL) {
            uint16_t s = ce_span_of_cp[cp];
            total = s != 0 ? ce_spans[s].len : 2;
        } else {
            for (int k = 0; k < d->len; k++) {
                uint16_t s = ce_span_of_cp[decomp_pool[d->offset + k]];
                total += s != 0 ? ce_spans[s].len : 2;
            }
        }
        if (total > runtime_cap) {
            fprintf(stderr, "U+%04lX needs %d CEs, over the runtime queue of %d\n",
                    cp, total, runtime_cap);
            exit(1);
        }
    }
}

static void parse_prop_list(void)
{
    FILE *f = open_or_die("tools/ucd/PropList.txt", "r");
    char line[1024];
    while (fgets(line, sizeof line, f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        // Exact property-name match.
        char *semi = strchr(line, ';');
        if (semi == NULL)
            continue;
        char prop[64];
        if (sscanf(semi + 1, " %63s", prop) != 1 || strcmp(prop, "White_Space") != 0)
            continue;
        long lo, hi;
        if (sscanf(line, "%lx..%lx", &lo, &hi) != 2) {
            if (sscanf(line, "%lx", &lo) != 1)
                continue;
            hi = lo;
        }
        for (long c = lo; c <= hi; c++)
            flags[c] |= FLAG_WHITESPACE;
    }
    fclose(f);
}

static void read_version(char *out, size_t cap)
{
    snprintf(out, cap, "unknown");
    FILE *f = fopen("tools/ucd/ReadMe.txt", "r");
    if (f == NULL)
        return;
    char line[1024];
    while (fgets(line, sizeof line, f) != NULL) {
        const char *v = strstr(line, "Version ");
        int a, b, c;
        if (v != NULL && sscanf(v, "Version %d.%d.%d", &a, &b, &c) == 3) {
            snprintf(out, cap, "%d.%d.%d", a, b, c);
            break;
        }
    }
    fclose(f);
}

// Dedupe 256-entry blocks of map[0..TABLE_CP) into stage1/stage2. Block 0 = all-zero.
static void build_two_stage(const uint16_t *map)
{
    memset(stage2, 0, 256 * sizeof stage2[0]);
    block_count = 1;
    for (size_t b = 0; b < BLOCK_COUNT; b++) {
        const uint16_t *block = &map[b * 256];
        size_t idx = SIZE_MAX;
        for (size_t k = 0; k < block_count; k++) {
            if (memcmp(&stage2[k * 256], block, 256 * sizeof block[0]) == 0) {
                idx = k;
                break;
            }
        }
        if (idx == SIZE_MAX) {
            memcpy(&stage2[block_count * 256], block, 256 * sizeof block[0]);
            idx = block_count++;
        }
        stage1[b] = (uint16_t)idx;
    }
}

static void build_tables(void)
{
    // Record 0 is the all-zero identity record: unassigned, caseless, and every code
    // point outside the keep-list (SMP included, runtime guards cp >= TABLE_CP).
    records[0] = (record_t){0, 0, 0};
    record_count = 1;
    for (long cp = 0; cp < TABLE_CP; cp++) {
        if (!class_kept((uint32_t)cp)) {
            record_of_cp[cp] = 0;
            continue;
        }
        record_t r = { upper_delta[cp], lower_delta[cp], flags[cp] };
        size_t idx = SIZE_MAX;
        for (size_t k = 0; k < record_count; k++) {
            if (memcmp(&records[k], &r, sizeof r) == 0) {
                idx = k;
                break;
            }
        }
        if (idx == SIZE_MAX) {
            if (record_count >= sizeof records / sizeof records[0]) {
                fprintf(stderr, "record table overflow\n");
                exit(1);
            }
            records[record_count] = r;
            idx = record_count++;
        }
        record_of_cp[cp] = (uint16_t)idx;
    }

    build_two_stage(record_of_cp);
}

static void emit_u16_array(FILE *f, const char *name, const uint16_t *v, size_t n)
{
    fprintf(f, "static const uint16_t %s[%zu] = {\n", name, n);
    for (size_t i = 0; i < n; i += 16) {
        fputs("   ", f);
        for (size_t j = i; j < n && j < i + 16; j++)
            fprintf(f, " %u,", v[j]);
        fputc('\n', f);
    }
    fputs("};\n\n", f);
}

static void emit_u32_array(FILE *f, const char *name, const uint32_t *v, size_t n)
{
    fprintf(f, "static const uint32_t %s[%zu] = {\n", name, n);
    for (size_t i = 0; i < n; i += 8) {
        fputs("   ", f);
        for (size_t j = i; j < n && j < i + 8; j++)
            fprintf(f, " 0x%08Xu,", v[j]);
        fputc('\n', f);
    }
    fputs("};\n\n", f);
}

static void emit_collate_tables(const char *version)
{
    build_two_stage(ce_span_of_cp);

    const char *out_path = "src/strings/ano_collate_tables.h";
    FILE *f = open_or_die(out_path, "w");

    fprintf(f,
        "/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors\n"
        " *\n"
        " * SPDX-License-Identifier: LGPL-3.0 */\n"
        "/*  == Anoptic Game Engine v0.0000001 == */\n"
        "\n"
        "// GENERATED FILE -- do not edit. Produced by tools/gen_unicode_tables.c from the\n"
        "// DUCET (allkeys.txt) and UnicodeData.txt canonical decompositions, version %s.\n"
        "// Trimmed to the collate_keep script list (Latin, Greek, Cyrillic, Runic, kana,\n"
        "// punctuation) and BMP-bound; everything else falls back to implicit weights\n"
        "// = code point order (the runtime guards cp >= 0x10000).\n"
        "//\n"
        "// A collation element packs primary(16).secondary(11).tertiary(5) into one u32.\n"
        "// ano_ce_stage2[(size_t)ano_ce_stage1[cp >> 8] * 256 + (cp & 0xFF)] indexes\n"
        "// ano_ce_spans (u16, offset << 4 | len into ano_ce_pool). Span 0 = unlisted: the\n"
        "// runtime computes UCA implicit weights. Decompositions are fully expanded NFD:\n"
        "// bsearch ano_decomp_cp, u16 spans (offset << 3 | len) into ano_decomp_pool.\n"
        "\n"
        "#ifndef ANOPTIC_SRC_STRINGS_ANO_COLLATE_TABLES_H\n"
        "#define ANOPTIC_SRC_STRINGS_ANO_COLLATE_TABLES_H\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "#define ANO_COLL_UCA_VERSION \"%s\"\n"
        "\n",
        version, version);

    // Spans pack into u16: offset(12).len(4). Decomp side packs offset(13).len(3).
    // All decomposition pieces are BMP, pool is u16.
    static uint16_t spans_packed[sizeof ce_spans / sizeof ce_spans[0]];
    for (size_t k = 0; k < ce_span_count; k++) {
        if (ce_spans[k].offset >= (1u << 12) || ce_spans[k].len >= (1u << 4)) {
            fprintf(stderr, "CE span over u16 packing (offset %u len %u)\n",
                    ce_spans[k].offset, ce_spans[k].len);
            exit(1);
        }
        spans_packed[k] = (uint16_t)(ce_spans[k].offset << 4 | ce_spans[k].len);
    }
    emit_u32_array(f, "ano_ce_pool", ce_pool, ce_pool_count);
    emit_u16_array(f, "ano_ce_spans", spans_packed, ce_span_count);
    emit_u16_array(f, "ano_ce_stage2", stage2, block_count * 256);
    emit_u16_array(f, "ano_ce_stage1", stage1, BLOCK_COUNT);

    // Flat ASCII fast path: every ASCII cp maps to exactly one CE and never decomposes.
    // Asserted here.
    uint32_t ce_ascii[128];
    for (uint32_t cp = 0; cp < 128; cp++) {
        uint16_t span = ce_span_of_cp[cp];
        if (span == 0 || ce_spans[span].len != 1 || find_decomp(cp) != NULL) {
            fprintf(stderr, "ASCII fast-path assumption broken at U+%04X\n", cp);
            exit(1);
        }
        ce_ascii[cp] = ce_pool[ce_spans[span].offset];
    }
    emit_u32_array(f, "ano_ce_ascii", ce_ascii, 128);

    static uint16_t dc_cp[sizeof decomps / sizeof decomps[0]];
    static uint16_t dc_span[sizeof decomps / sizeof decomps[0]];
    static uint16_t dc_pool[sizeof decomp_pool / sizeof decomp_pool[0]];
    for (size_t k = 0; k < decomp_count; k++) {
        if (decomps[k].cp >= TABLE_CP || decomps[k].offset >= (1u << 13) ||
            decomps[k].len >= (1u << 3)) {
            fprintf(stderr, "decomposition over u16 packing at U+%04X\n", decomps[k].cp);
            exit(1);
        }
        dc_cp[k] = (uint16_t)decomps[k].cp;
        dc_span[k] = (uint16_t)(decomps[k].offset << 3 | decomps[k].len);
    }
    for (size_t k = 0; k < decomp_pool_count; k++) {
        if (decomp_pool[k] >= TABLE_CP) {
            fprintf(stderr, "decomposition piece outside the BMP\n");
            exit(1);
        }
        dc_pool[k] = (uint16_t)decomp_pool[k];
    }
    emit_u16_array(f, "ano_decomp_cp", dc_cp, decomp_count);
    emit_u16_array(f, "ano_decomp_span", dc_span, decomp_count);
    emit_u16_array(f, "ano_decomp_pool", dc_pool, decomp_pool_count);

    fputs("#endif // ANOPTIC_SRC_STRINGS_ANO_COLLATE_TABLES_H\n", f);
    fclose(f);

    size_t bytes = ce_pool_count * 4
                 + (ce_span_count + decomp_count * 2 + decomp_pool_count) * 2
                 + (block_count * 256 + BLOCK_COUNT) * 2;
    printf("DUCET: %zu spans, %zu CEs, %zu decompositions, %zu blocks\n",
           ce_span_count, ce_pool_count, decomp_count, block_count);
    printf("wrote %s (~%zu KB of tables)\n", out_path, bytes / 1024);
}

int main(void)
{
    parse_unicode_data();
    parse_prop_list();
    expand_decompositions();
    parse_allkeys();
    assert_ce_queue_bound(64);
    char version[32];
    read_version(version, sizeof version);
    build_tables();

    const char *out_path = "src/strings/ano_unicode_tables.h";
    FILE *f = open_or_die(out_path, "w");

    fprintf(f,
        "/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors\n"
        " *\n"
        " * SPDX-License-Identifier: LGPL-3.0 */\n"
        "/*  == Anoptic Game Engine v0.0000001 == */\n"
        "\n"
        "// GENERATED FILE -- do not edit. Produced by tools/gen_unicode_tables.c from\n"
        "// the Unicode Character Database, version %s.\n"
        "// Trimmed to the shipped scripts (collate_keep + Han) and BMP-bound: the runtime\n"
        "// treats cp >= 0x10000 as record 0 (uncased, no flags).\n"
        "//\n"
        "// Two-stage lookup:\n"
        "//     ano_uc_stage2[(size_t)ano_uc_stage1[cp >> 8] * 256 + (cp & 0xFF)]\n"
        "// yields an index into ano_uc_records. Record 0 is the identity record\n"
        "// (uncased, no flags), shared by unassigned, caseless, and unlisted code points.\n"
        "\n"
        "#ifndef ANOPTIC_SRC_STRINGS_ANO_UNICODE_TABLES_H\n"
        "#define ANOPTIC_SRC_STRINGS_ANO_UNICODE_TABLES_H\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "#define ANO_UC_UNICODE_VERSION \"%s\"\n"
        "#define ANO_UC_TABLE_MAX 0x10000u\n"
        "\n"
        "#define ANO_UC_LETTER     %uu\n"
        "#define ANO_UC_DIGIT      %uu\n"
        "#define ANO_UC_WHITESPACE %uu\n"
        "#define ANO_UC_MARK       %uu\n"
        "#define ANO_UC_PUNCT      %uu\n"
        "\n"
        "typedef struct ano_uc_record_t {\n"
        "    int32_t upper_delta;   // 0 when no simple uppercase mapping\n"
        "    int32_t lower_delta;   // 0 when no simple lowercase mapping\n"
        "    uint8_t flags;\n"
        "} ano_uc_record_t;\n"
        "\n",
        version, version, FLAG_LETTER, FLAG_DIGIT, FLAG_WHITESPACE, FLAG_MARK, FLAG_PUNCT);

    fprintf(f, "static const ano_uc_record_t ano_uc_records[%zu] = {\n", record_count);
    for (size_t k = 0; k < record_count; k++)
        fprintf(f, "    { %d, %d, %u },\n",
                records[k].upper_delta, records[k].lower_delta, records[k].flags);
    fputs("};\n\n", f);

    emit_u16_array(f, "ano_uc_stage2", stage2, block_count * 256);
    emit_u16_array(f, "ano_uc_stage1", stage1, BLOCK_COUNT);

    fputs("#endif // ANOPTIC_SRC_STRINGS_ANO_UNICODE_TABLES_H\n", f);
    fclose(f);

    size_t bytes = record_count * sizeof(record_t) + (block_count * 256 + BLOCK_COUNT) * 2;
    printf("Unicode %s: %zu records, %zu blocks\n", version, record_count, block_count);
    printf("wrote %s (~%zu KB of tables)\n", out_path, bytes / 1024);

    emit_collate_tables(version);
    return 0;
}
