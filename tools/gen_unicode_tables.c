/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Generates src/strings/ano_unicode_tables.h from the Unicode Character Database.
// A standalone offline tool, NOT part of the engine build -- the output is committed
// and only regenerated when moving to a new Unicode version.
//
// Usage (from the repository root):
//     curl -o tools/ucd/ReadMe.txt      https://www.unicode.org/Public/UCD/latest/ucd/ReadMe.txt
//     curl -o tools/ucd/UnicodeData.txt https://www.unicode.org/Public/UCD/latest/ucd/UnicodeData.txt
//     curl -o tools/ucd/PropList.txt    https://www.unicode.org/Public/UCD/latest/ucd/PropList.txt
//     gcc -O2 -o gen_unicode_tables tools/gen_unicode_tables.c
//     ./gen_unicode_tables
//
// Emits a two-stage lookup table covering every code point 0..0x10FFFF:
//     stage1[cp >> 8] -> block index -> stage2[block*256 + (cp & 0xFF)] -> record
// Each record holds the simple case-mapping deltas (0 = uncased/identity, per the
// Unicode convention that case mapping is total) and classification flags
// (letter = category L*, digit = Nd, mark = M*, whitespace = the White_Space property).
// Identical records and identical 256-entry blocks are deduplicated, which is what
// keeps full-Unicode coverage down to a few tens of KB.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CP 0x110000

#define FLAG_LETTER     1u
#define FLAG_DIGIT      2u
#define FLAG_WHITESPACE 4u
#define FLAG_MARK       8u

static uint8_t flags[MAX_CP];
static int32_t upper_delta[MAX_CP];
static int32_t lower_delta[MAX_CP];

typedef struct record_t {
    int32_t upper_delta;
    int32_t lower_delta;
    uint8_t flags;
} record_t;

static record_t records[4096];
static size_t   record_count;
static uint16_t record_of_cp[MAX_CP];

#define BLOCK_COUNT (MAX_CP / 256)
static uint16_t stage1[BLOCK_COUNT];
static uint16_t stage2[BLOCK_COUNT * 256];   // worst case: every block unique
static size_t   block_count;

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

static uint8_t category_flags(const char *cat)
{
    if (is_letter_category(cat)) return FLAG_LETTER;
    if (strcmp(cat, "Nd") == 0)  return FLAG_DIGIT;
    if (is_mark_category(cat))   return FLAG_MARK;
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
    }
    fclose(f);
}

static void parse_prop_list(void)
{
    FILE *f = open_or_die("tools/ucd/PropList.txt", "r");
    char line[1024];
    while (fgets(line, sizeof line, f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        // Exact property-name match: a substring test would also hit Pattern_White_Space.
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

static void build_tables(void)
{
    // Record 0 is the all-zero identity record, shared by unassigned/caseless space.
    records[0] = (record_t){0, 0, 0};
    record_count = 1;
    for (long cp = 0; cp < MAX_CP; cp++) {
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

    // Block 0 is the all-zero block.
    memset(stage2, 0, 256 * sizeof stage2[0]);
    block_count = 1;
    for (size_t b = 0; b < BLOCK_COUNT; b++) {
        const uint16_t *block = &record_of_cp[b * 256];
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

int main(void)
{
    parse_unicode_data();
    parse_prop_list();
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
        "//\n"
        "// Two-stage lookup over the full code space 0..0x10FFFF:\n"
        "//     ano_uc_stage2[(size_t)ano_uc_stage1[cp >> 8] * 256 + (cp & 0xFF)]\n"
        "// yields an index into ano_uc_records. Record 0 is the identity record\n"
        "// (uncased, no flags), shared by all unassigned and caseless code points.\n"
        "\n"
        "#ifndef ANOPTIC_SRC_STRINGS_ANO_UNICODE_TABLES_H\n"
        "#define ANOPTIC_SRC_STRINGS_ANO_UNICODE_TABLES_H\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "#define ANO_UC_UNICODE_VERSION \"%s\"\n"
        "\n"
        "#define ANO_UC_LETTER     %uu\n"
        "#define ANO_UC_DIGIT      %uu\n"
        "#define ANO_UC_WHITESPACE %uu\n"
        "#define ANO_UC_MARK       %uu\n"
        "\n"
        "typedef struct ano_uc_record_t {\n"
        "    int32_t upper_delta;   // 0 when no simple uppercase mapping\n"
        "    int32_t lower_delta;   // 0 when no simple lowercase mapping\n"
        "    uint8_t flags;\n"
        "} ano_uc_record_t;\n"
        "\n",
        version, version, FLAG_LETTER, FLAG_DIGIT, FLAG_WHITESPACE, FLAG_MARK);

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
    return 0;
}
