/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Draw-free theory: scales, chords, guides, counterpoint, pivots.
// Table contents and iteration orders are prototype-verbatim. Nothing here touches the RNG.

#include "music_theory.h"

#include <stdio.h>
#include <string.h>

// Euclidean mod for possibly-negative values (Python % semantics).
static inline int pymod(int a, int m)
{
    int r = a % m;
    return r < 0 ? r + m : r;
}

/* Scales */

const char *const ANO_MODE_NAMES[ANO_MODE_COUNT] = {
    "ionian", "dorian", "phrygian", "lydian", "mixolydian", "aeolian", "locrian",
};

static const uint8_t IONIAN[7] = { 0, 2, 4, 5, 7, 9, 11 };

static const char *const TONIC_NAMES[12] = {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
};

int ano_mode_brightness(AnoMode mode)
{
    switch (mode) {
    case ANO_MODE_LYDIAN:     return 3;
    case ANO_MODE_IONIAN:     return 2;
    case ANO_MODE_MIXOLYDIAN: return 1;
    case ANO_MODE_DORIAN:     return 0;
    case ANO_MODE_AEOLIAN:    return -1;
    case ANO_MODE_PHRYGIAN:   return -2;
    default:                  return -1; // BRIGHTNESS.get(mode, -1): locrian absent
    }
}

const uint8_t *ano_mode_intervals(AnoMode mode)
{
    static uint8_t table[ANO_MODE_COUNT][7];
    static bool baked = false;
    if (!baked) {
        for (int k = 0; k < ANO_MODE_COUNT; ++k) {
            int root = IONIAN[k];
            for (int i = 0; i < 7; ++i)
                table[k][i] = (uint8_t)pymod(IONIAN[(k + i) % 7] - root, 12);
        }
        baked = true;
    }
    return table[mode];
}

void ano_scale_pcs(AnoScale s, uint8_t out[7])
{
    const uint8_t *iv = ano_mode_intervals((AnoMode)s.mode);
    for (int i = 0; i < 7; ++i)
        out[i] = (uint8_t)((s.tonic + iv[i]) % 12);
}

bool ano_scale_contains(AnoScale s, int midi)
{
    uint8_t pcs[7];
    ano_scale_pcs(s, pcs);
    int pc = pymod(midi, 12);
    for (int i = 0; i < 7; ++i)
        if (pcs[i] == pc)
            return true;
    return false;
}

int ano_scale_degree_of(AnoScale s, int midi)
{
    uint8_t pcs[7];
    ano_scale_pcs(s, pcs);
    int pc = pymod(midi, 12);
    for (int i = 0; i < 7; ++i)
        if (pcs[i] == pc)
            return i + 1;
    return 0;
}

int ano_scale_pitch_at(AnoScale s, int degree, int octave)
{
    int step  = (degree - 1) % 7;
    int octUp = (degree - 1) / 7;
    return (octave + 1 + octUp) * 12 + s.tonic + ano_mode_intervals((AnoMode)s.mode)[step];
}

int ano_snap_to_scale(AnoScale s, int pitch)
{
    static const int deltas[5] = { 0, 1, -1, 2, -2 };
    for (int i = 0; i < 5; ++i)
        if (ano_scale_contains(s, pitch + deltas[i]))
            return pitch + deltas[i];
    return pitch;
}

int ano_diatonic_shift(AnoScale s, int pitch, int steps)
{
    int p = ano_snap_to_scale(s, pitch);
    int direction = steps > 0 ? 1 : -1;
    int n = steps < 0 ? -steps : steps;
    for (int i = 0; i < n; ++i) {
        int q = p + direction;
        while (!ano_scale_contains(s, q))
            q += direction;
        p = q;
    }
    return p;
}

const char *ano_scale_name(AnoScale s, char *buf, uint32_t cap)
{
    snprintf(buf, cap, "%s %s", TONIC_NAMES[s.tonic], ANO_MODE_NAMES[s.mode]);
    return buf;
}

/* Pitch */

const char *ano_pitch_name(int midi, bool preferFlats, char *buf, uint32_t cap)
{
    static const char *const SHARP[12] = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };
    static const char *const FLAT[12] = { "C", "Db", "D", "Eb", "E", "F",
                                          "Gb", "G", "Ab", "A", "Bb", "B" };
    snprintf(buf, cap, "%s%d", (preferFlats ? FLAT : SHARP)[pymod(midi, 12)],
             ano_octave_of(midi));
    return buf;
}

/* Chords */

static const char *const ROMAN[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

// tonic / pre-dominant / dominant by root degree (1-based)
static const char FUNCTION_OF_DEGREE[8] = { 0, 'T', 'P', 'T', 'P', 'D', 'T', 'D' };

AnoChord ano_chord_applied_dominant(int target, bool seventh)
{
    AnoChord c = ano_chord((target + 3) % 7 + 1, seventh ? ANO_EXT_7 : 0);
    c.applied = (uint8_t)target;
    return c;
}

char ano_chord_function(AnoChord c)
{
    return c.applied ? 'D' : FUNCTION_OF_DEGREE[c.degree];
}

uint32_t ano_chord_member_degrees(AnoChord c, int out[5])
{
    int d = c.degree;
    int third = d + 2;
    if (c.extensions & ANO_EXT_SUS2)
        third = d + 1;
    else if (c.extensions & ANO_EXT_SUS4)
        third = d + 3;
    uint32_t n = 0;
    out[n++] = d;
    out[n++] = third;
    out[n++] = d + 4;
    if (c.extensions & ANO_EXT_7)
        out[n++] = d + 6;
    if (c.extensions & ANO_EXT_9)
        out[n++] = d + 8;
    return n;
}

static AnoScale chord_scale_for(AnoChord c, AnoScale context)
{
    if (c.sourceMode == ANO_MODE_NONE || c.sourceMode == (int8_t)context.mode)
        return context;
    return (AnoScale){ context.tonic, (uint8_t)c.sourceMode };
}

uint32_t ano_chord_pitch_classes(AnoChord c, AnoScale context, uint8_t out[5])
{
    if (c.applied) {
        // dominant quality by pc offsets — the applied chord leaves the collection
        int rootPc = (ano_scale_pitch_at(context, c.applied, 4) + 7) % 12;
        static const int seven[4] = { 0, 4, 7, 10 };
        uint32_t n = (c.extensions & ANO_EXT_7) ? 4u : 3u;
        for (uint32_t i = 0; i < n; ++i)
            out[i] = (uint8_t)((rootPc + seven[i]) % 12);
        return n;
    }
    AnoScale source = chord_scale_for(c, context);
    int members[5];
    uint32_t n = ano_chord_member_degrees(c, members);
    for (uint32_t i = 0; i < n; ++i)
        out[i] = (uint8_t)(ano_scale_pitch_at(source, members[i], 4) % 12);
    return n;
}

uint32_t ano_chord_voiced_pcs(AnoChord c, AnoScale context, uint8_t out[5])
{
    uint8_t pcs[5];
    uint32_t n = ano_chord_pitch_classes(c, context, pcs);
    for (uint32_t i = 0; i < n; ++i)
        out[i] = pcs[(i + c.inversion) % n];
    return n;
}

int ano_chord_bass_pc(AnoChord c, AnoScale context)
{
    uint8_t pcs[5];
    ano_chord_voiced_pcs(c, context, pcs);
    return pcs[0];
}

AnoChordQuality ano_chord_quality(AnoChord c, AnoScale context)
{
    if (c.extensions & (ANO_EXT_SUS2 | ANO_EXT_SUS4))
        return ANO_QUAL_SUS;
    uint8_t pcs[5];
    ano_chord_pitch_classes(c, context, pcs);
    int third = pymod(pcs[1] - pcs[0], 12);
    int fifth = pymod(pcs[2] - pcs[0], 12);
    if (third == 4 && fifth == 7) return ANO_QUAL_MAJ;
    if (third == 3 && fifth == 7) return ANO_QUAL_MIN;
    if (third == 3 && fifth == 6) return ANO_QUAL_DIM;
    if (third == 4 && fifth == 8) return ANO_QUAL_AUG;
    return ANO_QUAL_UNKNOWN;
}

// lowercase a roman numeral in place-of-copy (ASCII only)
static void roman_cased(const char *numeral, bool lower, char *dst, size_t cap)
{
    size_t i = 0;
    for (; numeral[i] && i + 1 < cap; ++i)
        dst[i] = lower && numeral[i] >= 'A' && numeral[i] <= 'Z'
               ? (char)(numeral[i] + 32) : numeral[i];
    dst[i] = 0;
}

const char *ano_chord_symbol(AnoChord c, AnoScale context, char *buf, uint32_t cap)
{
    char numeral[8];
    if (c.applied) {
        AnoChord target = ano_chord(c.applied, 0);
        AnoChordQuality tq = ano_chord_quality(target, context);
        roman_cased(ROMAN[c.applied - 1], tq == ANO_QUAL_MIN || tq == ANO_QUAL_DIM,
                    numeral, sizeof numeral);
        snprintf(buf, cap, "V%s/%s", (c.extensions & ANO_EXT_7) ? "7" : "", numeral);
        return buf;
    }
    uint8_t pcs[5];
    ano_chord_pitch_classes(c, context, pcs);
    int diatonicPc = ano_scale_pitch_at(context, c.degree, 4) % 12;
    int shift = pymod(pcs[0] - diatonicPc, 12);
    const char *prefix = shift == 11 ? "b" : shift == 1 ? "#" : "";
    AnoChordQuality q = ano_chord_quality(c, context);
    roman_cased(ROMAN[c.degree - 1], q == ANO_QUAL_MIN || q == ANO_QUAL_DIM,
                numeral, sizeof numeral);

    int members[5];
    uint32_t memberCount = ano_chord_member_degrees(c, members);
    char figure[8] = "";
    if (c.inversion) {
        if (memberCount == 3) {
            if (c.inversion == 1) strcpy(figure, "6");
            else if (c.inversion == 2) strcpy(figure, "64");
            else snprintf(figure, sizeof figure, "/%d", c.inversion);
        } else {
            if (c.inversion == 1) strcpy(figure, "65");
            else if (c.inversion == 2) strcpy(figure, "43");
            else if (c.inversion == 3) strcpy(figure, "42");
            else snprintf(figure, sizeof figure, "/%d", c.inversion);
        }
    }
    char ext[16] = "";
    bool has7 = c.extensions & ANO_EXT_7, has9 = c.extensions & ANO_EXT_9;
    if (has7 && has9)
        strcpy(ext, "9");
    else if (has7 && figure[0] == 0)
        strcpy(ext, "7");
    else if (has9)
        strcpy(ext, "(add9)");
    snprintf(buf, cap, "%s%s%s%s%s%s%s", prefix, numeral,
             q == ANO_QUAL_DIM ? "\xC2\xB0" : "", ext,
             (c.extensions & ANO_EXT_SUS2) ? "sus2" : "",
             (c.extensions & ANO_EXT_SUS4) ? "sus4" : "", figure);
    return buf;
}

/* Guide tones */

void ano_guide_pcs(AnoChord c, AnoScale s, int out[2])
{
    uint8_t pcs[5];
    uint32_t n = ano_chord_pitch_classes(c, s, pcs);
    out[0] = pcs[1];
    out[1] = n > 3 ? pcs[3] : pcs[2];
}

int ano_next_guide(int prevPc, AnoChord c, AnoScale s)
{
    int cands[2];
    ano_guide_pcs(c, s, cands);
    if (prevPc < 0)
        return cands[0];
    int best = cands[0], bestKey = -1;
    for (int i = 0; i < 2; ++i) {
        int up = pymod(cands[i] - prevPc, 12);
        int dn = pymod(prevPc - cands[i], 12);
        int fold = up < dn ? up : dn;
        int key = fold * 16 + cands[i]; // (folded distance, pc) lexicographic
        if (bestKey < 0 || key < bestKey) {
            bestKey = key;
            best = cands[i];
        }
    }
    return best;
}

/* Counterpoint */

bool ano_is_perfect(int lower, int upper)
{
    int ic = ano_interval_class(lower, upper);
    return ic == 0 || ic == 7;
}

bool ano_is_consonant(int lower, int upper)
{
    switch (ano_interval_class(lower, upper)) {
    case 0: case 3: case 4: case 7: case 8: case 9: return true;
    default: return false;
    }
}

AnoMotion ano_motion(int prevLower, int prevUpper, int lower, int upper)
{
    int dl = lower - prevLower, du = upper - prevUpper;
    if (dl == 0 || du == 0)
        return ANO_MOTION_OBLIQUE;
    if ((dl > 0) != (du > 0))
        return ANO_MOTION_CONTRARY;
    return ano_interval_class(prevLower, prevUpper) == ano_interval_class(lower, upper)
         ? ANO_MOTION_PARALLEL : ANO_MOTION_SIMILAR;
}

bool ano_forbidden_parallel(int prevLower, int prevUpper, int lower, int upper)
{
    if (lower == prevLower || upper == prevUpper)
        return false;
    int ic = ano_interval_class(lower, upper);
    return (ic == 0 || ic == 7) && ano_interval_class(prevLower, prevUpper) == ic;
}

bool ano_forbidden_direct(int prevLower, int prevUpper, int lower, int upper, int maxStep)
{
    int dl = lower - prevLower, du = upper - prevUpper;
    if (dl == 0 || du == 0 || (dl > 0) != (du > 0))
        return false;
    int ic = ano_interval_class(lower, upper);
    if ((ic != 0 && ic != 7) || ano_interval_class(prevLower, prevUpper) == ic)
        return false;
    return (du < 0 ? -du : du) > maxStep;
}

/* Pivot modulation */

// pivot preference by NEW-key degree; old-dominant chords pull backward
static const int NEW_DEGREE_RANK[8] = { 0, 3, 0, 4, 1, 8, 2, 9 }; // index by degree
#define OLD_DOMINANT_PENALTY 6

static int pivot_score(const AnoPivot *p)
{
    int penalty = FUNCTION_OF_DEGREE[p->oldDegree] == 'D' ? OLD_DOMINANT_PENALTY : 0;
    // (rank+penalty, newDegree, oldDegree) packed lexicographically
    return (NEW_DEGREE_RANK[p->newDegree] + penalty) * 64 + p->newDegree * 8 + p->oldDegree;
}

static uint16_t pcs_mask(const uint8_t *pcs, uint32_t n)
{
    uint16_t m = 0;
    for (uint32_t i = 0; i < n; ++i)
        m |= (uint16_t)(1u << pcs[i]);
    return m;
}

uint32_t ano_find_pivots(AnoScale oldKey, AnoScale newKey, AnoPivot out[7])
{
    uint16_t newMask[8] = {0}; // by new degree; 0 = unusable
    for (int d = 1; d <= 7; ++d) {
        AnoChord c = ano_chord(d, 0);
        if (ano_chord_quality(c, newKey) == ANO_QUAL_DIM)
            continue;
        uint8_t pcs[5];
        uint32_t n = ano_chord_pitch_classes(c, newKey, pcs);
        newMask[d] = pcs_mask(pcs, n);
    }
    uint32_t count = 0;
    for (int d = 1; d <= 7; ++d) {
        AnoChord c = ano_chord(d, 0);
        if (ano_chord_quality(c, oldKey) == ANO_QUAL_DIM)
            continue;
        uint8_t pcs[5];
        uint32_t n = ano_chord_pitch_classes(c, oldKey, pcs);
        uint16_t m = pcs_mask(pcs, n);
        for (int nd = 1; nd <= 7; ++nd) {
            if (newMask[nd] == m) {
                AnoPivot *p = &out[count++];
                p->oldDegree = (uint8_t)d;
                p->newDegree = (uint8_t)nd;
                for (int i = 0; i < 3; ++i)
                    p->pcs[i] = pcs[i];
                break;
            }
        }
    }
    // ascending by the packed score (unique per pivot: no stable-sort concern)
    for (uint32_t i = 1; i < count; ++i) {
        AnoPivot key = out[i];
        int ks = pivot_score(&key);
        uint32_t j = i;
        while (j > 0 && pivot_score(&out[j - 1]) > ks) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    return count;
}

int ano_fifths_between(int a, int b)
{
    int k = pymod(7 * (b - a), 12);
    return k <= 6 ? k : k - 12;
}
