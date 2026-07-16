/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Motif factories, variation, realizers. Contour offsets: banker's over exact float op order.
// max-by-key: first maximal (strict >). ornament/phrase_variant draw only on prototype paths.
// _pick_base: int-tuple order flips with prefer_fit.

#include "music_motif.h"

AnoMelodyConfig ano_melody_config_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoMelodyConfig k = {
        .rangeSemitones = 12, .barRestMax = 0.30, .spanMin = 2, .spanMax = 4,
        .planApex = false, .counterpoint = false,
    };
    return k;
}

void ano_contour_offsets(AnoContourShape shape, uint32_t n, int span,
                         int out[ANO_MOTIF_MAX])
{
    if (n == 1) {
        out[0] = 0;
        return;
    }
    for (uint32_t i = 0; i < n && i < ANO_MOTIF_MAX; ++i) {
        switch (shape) {
        case ANO_SHAPE_ARCH: {
            double x = 2.0 * i / (double)(n - 1) - 1.0;
            if (x < 0.0)
                x = -x;
            out[i] = (int)ano_music_round_int(span * (1.0 - x));
            break;
        }
        case ANO_SHAPE_DESCENT:
            out[i] = (int)ano_music_round_int(span * (1.0 - i / (double)(n - 1)));
            break;
        case ANO_SHAPE_ASCENT:
            out[i] = (int)ano_music_round_int(span * i / (double)(n - 1));
            break;
        default: // rising zigzag
            out[i] = (int)(i / 2) + (i % 2 ? 2 : 0);
            break;
        }
    }
}

AnoMotif ano_make_motif(AnoMusicRng *rng, double density, double roughness,
                        const AnoMelodyConfig *cfg, int slots)
{
    AnoMotif m = { 0 };
    m.n = ano_rough_cell(rng, density, roughness, slots, 2, m.rhythm);
    m.shape = (uint8_t)ano_music_choice(rng, ANO_SHAPE_COUNT);
    int span = (int)ano_music_randint(rng, cfg->spanMin, cfg->spanMax);
    ano_contour_offsets((AnoContourShape)m.shape, m.n, span, m.contour);
    return m;
}

int ano_motif_markedness(const AnoMotif *m)
{
    int deltas[ANO_MOTIF_MAX];
    uint32_t nd = 0;
    for (uint32_t i = 1; i < m->n; ++i)
        deltas[nd++] = m->contour[i] - m->contour[i - 1];
    int turns[ANO_MOTIF_MAX];
    uint32_t nt = 0;
    for (uint32_t i = 0; i < nd; ++i)
        if (deltas[i])
            turns[nt++] = deltas[i];
    bool motto = m->n >= 4 && m->n <= 7;
    bool twoDurs = false;
    for (uint32_t i = 1; i < m->n && !twoDurs; ++i)
        for (uint32_t j = 0; j < i; ++j)
            if (m->rhythm[i].durSlots != m->rhythm[j].durSlots) {
                twoDurs = true;
                break;
            }
    bool leap = false, step = false;
    for (uint32_t i = 0; i < nd; ++i) {
        int a = deltas[i] < 0 ? -deltas[i] : deltas[i];
        if (a >= 2)
            leap = true;
        if (a <= 1)
            step = true;
    }
    bool turn = false;
    for (uint32_t i = 1; i < nt; ++i)
        if ((turns[i - 1] > 0) != (turns[i] > 0))
            turn = true;
    return (int)motto + (int)twoDurs + (int)leap + (int)step + (int)turn;
}

AnoMotif ano_make_signature(AnoMusicRng *rng, double density, double roughness,
                            const AnoMelodyConfig *cfg, int slots, int attempts)
{
    // clamp into the motto zone
    if (density < 0.5)
        density = 0.5;
    if (density > 0.75)
        density = 0.75;
    if (roughness < 0.3)
        roughness = 0.3;
    // max(..., key=_markedness): first of equals wins
    AnoMotif best = ano_make_motif(rng, density, roughness, cfg, slots);
    int bestScore = ano_motif_markedness(&best);
    for (int a = 1; a < attempts; ++a) {
        AnoMotif cand = ano_make_motif(rng, density, roughness, cfg, slots);
        int score = ano_motif_markedness(&cand);
        if (score > bestScore) {
            best = cand;
            bestScore = score;
        }
    }
    // tail merge until motto length and >= 2 durations
    for (;;) {
        bool tooLong = best.n > 7;
        bool flat = false;
        if (best.n > 1) {
            flat = true;
            for (uint32_t i = 1; i < best.n && flat; ++i)
                if (best.rhythm[i].durSlots != best.rhythm[0].durSlots)
                    flat = false;
        }
        if (!tooLong && !flat)
            break;
        int s1 = best.rhythm[best.n - 2].slot;
        int s2 = best.rhythm[best.n - 1].slot;
        int d2 = best.rhythm[best.n - 1].durSlots;
        best.rhythm[best.n - 2] = (AnoRhythmNote){ s1, s2 + d2 - s1 };
        best.n -= 1;
    }
    // midpoint lift when the contour lacks a leap-and-turn
    int deltas[ANO_MOTIF_MAX];
    uint32_t nd = 0;
    for (uint32_t i = 1; i < best.n; ++i)
        deltas[nd++] = best.contour[i] - best.contour[i - 1];
    int turns[ANO_MOTIF_MAX];
    uint32_t nt = 0;
    for (uint32_t i = 0; i < nd; ++i)
        if (deltas[i])
            turns[nt++] = deltas[i];
    bool leap = false;
    for (uint32_t i = 0; i < nd; ++i)
        if ((deltas[i] < 0 ? -deltas[i] : deltas[i]) >= 2)
            leap = true;
    bool turn = false;
    for (uint32_t i = 1; i < nt; ++i)
        if ((turns[i - 1] > 0) != (turns[i] > 0))
            turn = true;
    if (!(leap && turn))
        best.contour[best.n / 2] += 2;
    return best;
}

AnoApexPlan ano_make_apex(AnoMusicRng *rng, int bars, int center, int rangeSemitones)
{
    static const double W[2] = { 0.45, 0.55 };
    int picks[2] = { bars - 3, bars - 2 };
    int pos = picks[ano_music_choices1(rng, W, 2)];
    if (pos < 1)
        pos = 1;
    int loOff = rangeSemitones * 5 / 12;
    if (loOff < 3)
        loOff = 3;
    int hiEnd = rangeSemitones - 1;
    if (hiEnd < loOff + 1)
        hiEnd = loOff + 1;
    return (AnoApexPlan){ pos, center + (int)ano_music_randint(rng, loOff, hiEnd) };
}

AnoMotif ano_motif_sequence(const AnoMotif *m, int steps)
{
    AnoMotif out = *m;
    for (uint32_t i = 0; i < out.n; ++i)
        out.contour[i] += steps;
    return out;
}

AnoMotif ano_motif_invert(const AnoMotif *m)
{
    AnoMotif out = *m;
    for (uint32_t i = 0; i < out.n; ++i)
        out.contour[i] = -out.contour[i];
    return out;
}

AnoMotif ano_motif_displace(const AnoMotif *m, int slots)
{
    AnoMotif out = *m;
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n; ++i) {
        int s = m->rhythm[i].slot + 2;
        if (s + m->rhythm[i].durSlots <= slots) {
            out.rhythm[n] = (AnoRhythmNote){ s, m->rhythm[i].durSlots };
            out.contour[n] = m->contour[n]; // contour[:len(shifted)]
            n++;
        }
    }
    if (n == 0)
        return *m;
    out.n = n;
    return out;
}

AnoMotif ano_motif_truncate(const AnoMotif *m)
{
    if (m->n <= 2)
        return *m;
    AnoMotif out = *m;
    out.n = m->n - 1;
    return out;
}

AnoMotif ano_motif_ornament(const AnoMotif *m, AnoMusicRng *rng)
{
    // first longest note; no draw when it cannot split
    uint32_t idx = 0;
    for (uint32_t i = 1; i < m->n; ++i)
        if (m->rhythm[i].durSlots > m->rhythm[idx].durSlots)
            idx = i;
    int s = m->rhythm[idx].slot;
    int d = m->rhythm[idx].durSlots;
    if (d < 2)
        return *m;
    AnoMotif out = { 0 };
    out.shape = m->shape;
    uint32_t n = 0;
    for (uint32_t i = 0; i < idx; ++i) {
        out.rhythm[n] = m->rhythm[i];
        out.contour[n] = m->contour[i];
        n++;
    }
    out.rhythm[n] = (AnoRhythmNote){ s, d / 2 };
    out.contour[n] = m->contour[idx];
    n++;
    static const int PM[2] = { -1, 1 };
    out.rhythm[n] = (AnoRhythmNote){ s + d / 2, d - d / 2 };
    out.contour[n] = m->contour[idx] + PM[ano_music_choice(rng, 2)];
    n++;
    for (uint32_t i = idx + 1; i < m->n && n < ANO_MOTIF_MAX; ++i) {
        out.rhythm[n] = m->rhythm[i];
        out.contour[n] = m->contour[i];
        n++;
    }
    out.n = n;
    return out;
}

AnoMotif ano_motif_transform(const AnoMotif *m, AnoTransform t, int slots)
{
    switch (t) {
    case ANO_XFORM_INVERSION:    return ano_motif_invert(m);
    case ANO_XFORM_DISPLACEMENT: return ano_motif_displace(m, slots);
    case ANO_XFORM_TRUNCATION:   return ano_motif_truncate(m);
    default:                     return *m;
    }
}

AnoMotif ano_phrase_variant(const AnoMotif *m, int pos, AnoMusicRng *rng, int slots,
                            AnoVariantOp *outOp, int *outStep)
{
    if (outStep)
        *outStep = 0;
    if (pos == 0) {
        if (outOp)
            *outOp = ANO_VAR_STATEMENT;
        return *m;
    }
    if (pos == 1 || pos == 4) {
        static const int STEPS[4] = { -2, -1, 1, 2 };
        int step = STEPS[ano_music_choice(rng, 4)];
        if (outOp)
            *outOp = ANO_VAR_SEQUENCE;
        if (outStep)
            *outStep = step;
        return ano_motif_sequence(m, step);
    }
    if (pos == 3) {
        if (outOp)
            *outOp = ANO_VAR_RESTATEMENT;
        return *m;
    }
    if (pos == 6) {
        if (outOp)
            *outOp = ANO_VAR_ORNAMENT;
        return ano_motif_ornament(m, rng);
    }
    static const AnoVariantOp OPS[3] = { ANO_VAR_INVERT, ANO_VAR_DISPLACE, ANO_VAR_TRUNCATE };
    AnoVariantOp op = OPS[ano_music_choice(rng, 3)];
    if (outOp)
        *outOp = op;
    switch (op) {
    case ANO_VAR_INVERT:   return ano_motif_invert(m);
    case ANO_VAR_DISPLACE: return ano_motif_displace(m, slots);
    default:               return ano_motif_truncate(m);
    }
}

uint8_t ano_lifecycle_advance(AnoMotifLifecycle *lc, bool spend, int phrase)
{
    if (spend && lc->statements >= lc->developAfter) {
        lc->state = ANO_LIFE_COMPLETED;
        lc->completedPhrase = phrase;
    } else if (lc->state == ANO_LIFE_COMPLETED) {
        lc->state = ANO_LIFE_DEVELOPED; // the landing is a one-phrase event
        lc->statements += 1;
    } else {
        lc->statements += 1;
        lc->state = lc->statements > lc->developAfter ? ANO_LIFE_DEVELOPED
                                                      : ANO_LIFE_INTRODUCED;
    }
    return lc->state;
}

int ano_diatonic_interval(AnoScale scale, int a, int b)
{
    a = ano_snap_to_scale(scale, a);
    b = ano_snap_to_scale(scale, b);
    if (a == b)
        return 0;
    int lo = a < b ? a : b;
    int hi = a < b ? b : a;
    int steps = 0, p = lo;
    while (p < hi) {
        p = ano_diatonic_shift(scale, p, 1);
        steps += 1;
    }
    return b > a ? steps : -steps;
}

static void pitches_at(const AnoMotif *m, AnoScale scale, int base,
                       int out[ANO_MOTIF_MAX])
{
    for (uint32_t i = 0; i < m->n; ++i)
        out[i] = ano_diatonic_shift(scale, base, m->contour[i]);
}

static bool pc_member(int pitch, const uint8_t *pcs, uint32_t pcCount)
{
    int pc = (pitch % 12 + 12) % 12;
    for (uint32_t i = 0; i < pcCount; ++i)
        if (pcs[i] == pc)
            return true;
    return false;
}

// The transposition of the whole cell: score tuple is
// (in_range, strong_hits, dist) with prefer_fit, else (in_range, dist,
// strong_hits); max keeps the first maximal base.
static int pick_base(const AnoMotif *m, AnoScale scale,
                     const uint8_t *chordPcs, uint32_t pcCount,
                     int lo, int hi, uint32_t strongMask, int ref, bool preferFit)
{
    int best = INT_MIN;
    int b0 = 0, b1 = 0, b2 = 0;
    for (int base = lo; base <= hi; ++base) {
        if (!ano_scale_contains(scale, base))
            continue;
        int p[ANO_MOTIF_MAX];
        pitches_at(m, scale, base, p);
        int inRange = 0, strongHits = 0;
        for (uint32_t i = 0; i < m->n; ++i) {
            if (lo <= p[i] && p[i] <= hi)
                inRange++;
            if ((strongMask >> m->rhythm[i].slot & 1u)
                && pc_member(p[i], chordPcs, pcCount))
                strongHits++;
        }
        int d = p[0] - ref;
        int dist = -(d < 0 ? -d : d);
        int k0 = inRange;
        int k1 = preferFit ? strongHits : dist;
        int k2 = preferFit ? dist : strongHits;
        if (best == INT_MIN || k0 > b0 || (k0 == b0 && (k1 > b1 || (k1 == b1 && k2 > b2)))) {
            best = base;
            b0 = k0;
            b1 = k1;
            b2 = k2;
        }
    }
    if (best == INT_MIN)
        best = (lo + hi) / 2; // no in-scale base: the single fallback candidate
    return best;
}

uint32_t ano_realize_faithful(const AnoMotif *m, AnoScale scale,
                              const uint8_t *chordPcs, uint32_t pcCount,
                              int lo, int hi, uint32_t strongMask, int near,
                              AnoPlacedNote out[ANO_MOTIF_MAX])
{
    int ref = near != ANO_NEAR_NONE ? near : (lo + hi) / 2;
    int base = pick_base(m, scale, chordPcs, pcCount, lo, hi, strongMask, ref, true);
    int p[ANO_MOTIF_MAX];
    pitches_at(m, scale, base, p);
    for (uint32_t i = 0; i < m->n; ++i)
        out[i] = (AnoPlacedNote){ m->rhythm[i].slot, m->rhythm[i].durSlots, p[i] };
    return m->n;
}

uint32_t ano_realize_cadential(const AnoMotif *m, AnoScale scale,
                               const uint8_t *targetPcs, uint32_t pcCount,
                               int lo, int hi, int near, int slots,
                               AnoPlacedNote out[ANO_MOTIF_MAX])
{
    int ref = near != ANO_NEAR_NONE ? near : (lo + hi) / 2;
    // score tuple: (final-on-target, in_range, -|entry - ref|)
    int best = INT_MIN;
    int b0 = 0, b1 = 0, b2 = 0;
    for (int base = lo; base <= hi; ++base) {
        if (!ano_scale_contains(scale, base))
            continue;
        int p[ANO_MOTIF_MAX];
        pitches_at(m, scale, base, p);
        int onTarget = pc_member(p[m->n - 1], targetPcs, pcCount) ? 1 : 0;
        int inRange = 0;
        for (uint32_t i = 0; i < m->n; ++i)
            if (lo <= p[i] && p[i] <= hi)
                inRange++;
        int d = p[0] - ref;
        int dist = -(d < 0 ? -d : d);
        if (best == INT_MIN || onTarget > b0
            || (onTarget == b0 && (inRange > b1 || (inRange == b1 && dist > b2)))) {
            best = base;
            b0 = onTarget;
            b1 = inRange;
            b2 = dist;
        }
    }
    if (best == INT_MIN)
        best = (lo + hi) / 2;
    int p[ANO_MOTIF_MAX];
    pitches_at(m, scale, best, p);
    for (uint32_t i = 0; i < m->n; ++i)
        out[i] = (AnoPlacedNote){ m->rhythm[i].slot, m->rhythm[i].durSlots, p[i] };
    // the landing holds to the bar end
    int lastSlot = out[m->n - 1].slot;
    int hold = slots - lastSlot;
    if (out[m->n - 1].durSlots < hold)
        out[m->n - 1].durSlots = hold;
    return m->n;
}

double ano_motif_fit(const AnoMotif *m, AnoScale scale,
                     const uint8_t *chordPcs, uint32_t pcCount,
                     int lo, int hi, uint32_t strongMask, int near)
{
    int ref = near != ANO_NEAR_NONE ? near : (lo + hi) / 2;
    int base = pick_base(m, scale, chordPcs, pcCount, lo, hi, strongMask, ref, false);
    int p[ANO_MOTIF_MAX];
    pitches_at(m, scale, base, p);
    int strong = 0, onChord = 0;
    for (uint32_t i = 0; i < m->n; ++i)
        if (strongMask >> m->rhythm[i].slot & 1u) {
            strong++;
            if (pc_member(p[i], chordPcs, pcCount))
                onChord++;
        }
    if (strong == 0)
        return 1.0;
    return (double)onChord / (double)strong;
}

double ano_recognizability(const AnoMotif *m, const int *pitches, uint32_t n,
                           AnoScale scale)
{
    if (n < 2)
        return 1.0;
    uint32_t want = m->n - 1; // len(zip(want, got)) == min, equal by contract
    uint32_t pairs = n - 1 < want ? n - 1 : want;
    int match = 0;
    for (uint32_t i = 0; i < pairs; ++i) {
        int w = m->contour[i + 1] - m->contour[i];
        int g = ano_diatonic_interval(scale, pitches[i], pitches[i + 1]);
        if (w == g)
            match++;
    }
    return (double)match / (double)want;
}
