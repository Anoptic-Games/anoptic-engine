/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// advance_bar: extension -> dramaturg -> codetta/elision -> wander -> period ->
// instruments -> mode -> params -> withhold/escalate/hyper/texture/counter ->
// lifecycle -> apex -> chord queue -> context -> director ->
// layers (pad/bass/melody+imitation+counter/arp/perc) ->
// raw sort -> per-layer chains -> final sort -> modulation arrival.
// Stream tags: colon-joined, seed first. State mutation ORDER is load-bearing.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "music_conductor.h"
#include "music_gen.h"
#include "music_imitation.h"
#include "music_modifiers.h"
#include "music_pad.h"




static const int8_t DEFAULT_CADENCE_CYCLE[4] = {
    ANO_CADENCE_AUTHENTIC, ANO_CADENCE_HALF, ANO_CADENCE_DECEPTIVE, ANO_CADENCE_AUTHENTIC,
};
#define MIN_MELODY_RANGE 6
#define LANDMARK_IMPORTANCE 0.8
static const int LAMENT_CYCLE[4][2] = { { 1, 0 }, { 5, 1 }, { 4, 1 }, { 5, 0 } };
// the texture ladder, lean -> rich (indices into AnoTexture - 1)
static const AnoTexture TEXTURES[5] = {
    ANO_TEX_MONOPHONIC, ANO_TEX_HOMOPHONIC, ANO_TEX_DOUBLED,
    ANO_TEX_IMITATIVE, ANO_TEX_COUNTER,
};
#define TEXTURE_RETURN_PROB 0.25

AnoEngineConfig ano_engine_config_default(void)
{
    // memset: initializer leaves padding unspecified; snapshot = bytes.
    AnoEngineConfig c;
    memset(&c, 0, sizeof c);
    c.meter = ano_meter_default();
    c.params = ano_gen_params_default();
    c.mode = ANO_MODE_NONE;
    c.valence = 0.3;
    c.energy = 0.5;
    c.tension = 0.45;
    c.phraseBars = 8;
    c.wanderPhrases = -1;
    c.motifLeniency = 0.5;
    c.form.periodProb = 0.65;
    c.clock.codettaPayoff = 0.45;
    c.clock.codettaBars = 2;
    c.clock.extensionTension = 0.7;
    c.clock.elisionEnergy = 0.75;
    c.useChains = true;
    c.harmony = ano_harmony_config_default();
    c.voicing = ano_voicing_config_default();
    c.bass = ano_bass_config_default();
    c.melody = ano_melody_config_default();
    c.counter = ano_counter_config_default();
    c.arp = ano_arp_config_default();
    c.perc = ano_perc_config_default();
    return c;
}

/* Helpers */

static void eng_stream1(const AnoMusicEngine *e, AnoMusicRng *r, const char *name)
{
    char tag[64];
    snprintf(tag, sizeof tag, "%llu:%s", (unsigned long long)e->seed, name);
    ano_music_stream(r, tag);
}

static void eng_stream2(const AnoMusicEngine *e, AnoMusicRng *r, const char *name, int key)
{
    char tag[64];
    snprintf(tag, sizeof tag, "%llu:%s:%d", (unsigned long long)e->seed, name, key);
    ano_music_stream(r, tag);
}

static void eng_stream3(const AnoMusicEngine *e, AnoMusicRng *r, const char *a,
                        const char *b, int key)
{
    char tag[64];
    snprintf(tag, sizeof tag, "%llu:%s:%s:%d", (unsigned long long)e->seed, a, b, key);
    ano_music_stream(r, tag);
}

static bool layers_has(const uint8_t *layers, uint32_t n, uint8_t layer)
{
    for (uint32_t i = 0; i < n; ++i)
        if (layers[i] == layer)
            return true;
    return false;
}

static bool dramaturg_on(const AnoMusicEngine *e)
{
    return e->config.hasDramaturg && e->config.dramaturg.enabled;
}

static bool lifecycle_on(const AnoMusicEngine *e)
{
    return dramaturg_on(e) && e->config.dramaturg.motifLifecycle;
}

static bool director_on(const AnoMusicEngine *e)
{
    return e->st.director.libraryCount > 0;
}

static AnoPhrasePos pos_of(AnoMusicEngine *e, int bar)
{
    return ano_clock_position(&e->st.clock, bar);
}

static int8_t ledger_cadence(const AnoMusicEngine *e, int phrase)
{
    return ano_ledger_cadence(&e->st.ledger, phrase);
}

static int8_t policy_of(AnoMusicEngine *e, int phrase)
{
    const AnoEngineConfig *cfg = &e->config;
    if (e->st.hasModulation && e->st.modulation.hasCadencePhrase
        && phrase == e->st.modulation.cadencePhrase)
        return ANO_CADENCE_AUTHENTIC; // the modulation IS this phrase's cadence
    if (e->overrides.hasCadencePolicy)
        return e->overrides.cadencePolicy;
    if (dramaturg_on(e)) {
        int8_t forced = ledger_cadence(e, phrase);
        if (forced != ANO_CADENCE_NONE)
            return forced;
    }
    if (cfg->form.periods) {
        AnoPhraseForm role = ano_planner_role(&e->st.planner, phrase);
        if (role == ANO_FORM_ANTECEDENT)
            return ANO_CADENCE_HALF;
        if (role == ANO_FORM_CONSEQUENT)
            return ANO_CADENCE_AUTHENTIC;
    }
    if (cfg->cadencePolicyCount)
        return cfg->cadencePolicies[phrase % (int)cfg->cadencePolicyCount];
    if (!cfg->hasMapper)
        return DEFAULT_CADENCE_CYCLE[phrase % 4];
    if (phrase >= 0) {
        uint32_t sl = ano_ring_phrase(phrase);
        if (!ano_phrase_live(e->st.policyTag, phrase)) {
            e->st.policyTag[sl] = phrase;
            e->st.phrasePolicies[sl] =
                (int8_t)ano_pick_cadence_policy(e->affect.tension, &cfg->mapper);
        }
        return e->st.phrasePolicies[sl];
    }
    return ANO_CADENCE_AUTHENTIC;
}

static double harmonic_rhythm(const AnoMusicEngine *e)
{
    if (e->overrides.hasHarmonicRhythm)
        return e->overrides.harmonicRhythm;
    if (e->config.hasMapper)
        return ano_map_harmonic_rhythm(e->affect, &e->config.mapper);
    return e->config.params.harmonicRhythm;
}

static double rit_depth(AnoMusicEngine *e, AnoPhrasePos pos)
{
    if (e->config.cadenceRit <= 0 || ano_phrase_slot(pos) != ANO_SLOT_CADENCE)
        return 0.0;
    int8_t policy = policy_of(e, pos.phrase);
    double depth = policy == ANO_CADENCE_AUTHENTIC ? 1.0
                 : policy == ANO_CADENCE_HALF ? 0.5 : 0.0;
    depth *= e->config.cadenceRit;
    if (depth != 0.0 && dramaturg_on(e)
        && ledger_cadence(e, pos.phrase) == ANO_CADENCE_AUTHENTIC)
        depth *= 1.0 + 0.5 * e->st.ledger.lastSpend;
    return depth;
}

static int tonic_for_bar(const AnoMusicEngine *e, int bar)
{
    if (e->st.hasModulation && bar >= e->st.modulation.dominantBar)
        return e->st.modulation.targetTonic;
    return e->st.keyTonic;
}

static int wander_target(AnoMusicEngine *e, int phrase)
{
    int dist = ano_fifths_between(e->config.keyTonic, e->st.keyTonic);
    int step;
    if ((dist < 0 ? -dist : dist) >= 2) {
        step = dist > 0 ? -1 : 1; // the spring back toward home
    } else {
        double v = e->affect.valence;
        double lean = v > 0.15 ? 0.35 : v < -0.15 ? -0.35 : 0.0;
        AnoMusicRng r;
        eng_stream2(e, &r, "wander", phrase);
        step = ano_music_random(&r) < 0.5 + lean ? 1 : -1;
    }
    return ((e->st.keyTonic + 7 * step) % 12 + 12) % 12;
}

static void plan_modulation(AnoMusicEngine *e, int pivotBar, int target, bool aligned)
{
    AnoModulationPlan p = { 0 };
    int mode = e->st.currentMode;
    AnoPivot pivots[7];
    uint32_t pn = ano_find_pivots((AnoScale){ (uint8_t)e->st.keyTonic, (uint8_t)mode },
                                  (AnoScale){ (uint8_t)target, (uint8_t)mode }, pivots);
    p.targetTonic = target;
    p.mode = mode;
    p.pivotBar = pivotBar;
    p.dominantBar = pivotBar + 1;
    p.arrivalBar = pivotBar + 2;
    p.hasPivot = pn > 0;
    if (pn)
        p.pivot = pivots[0];
    p.hasCadencePhrase = aligned;
    if (aligned)
        p.cadencePhrase = pos_of(e, p.arrivalBar).phrase;
    e->st.modulation = p;
    e->st.hasModulation = true;
}

// Modulation-window chord. false = no usable pivot (walk proceeds).
static bool modulation_chord(AnoMusicEngine *e, int bar, AnoPhrasePos pos, AnoChord *out)
{
    const AnoModulationPlan *plan = &e->st.modulation;
    if (bar == plan->pivotBar) {
        if (!plan->hasPivot)
            return false;
        *out = ano_chord(plan->pivot.oldDegree, 0);
        return true;
    }
    if (bar == plan->dominantBar) {
        double tension = ano_effective_tension(e->affect.tension, pos);
        *out = ano_chord(5, tension >= 0.75 ? (ANO_EXT_7 | ANO_EXT_9) : ANO_EXT_7);
        return true;
    }
    *out = ano_chord(1, 0);
    return true;
}

static bool wants_split(AnoMusicEngine *e, AnoPhrasePos pos)
{
    const AnoEngineConfig *cfg = &e->config;
    if (!(cfg->form.split64 && cfg->form.cadential64) || pos.bars < 4
        || pos.kind == ANO_SEG_CODETTA)
        return false;
    if (pos.phrase < 0)
        return false;
    uint32_t sp = ano_ring_phrase(pos.phrase);
    if (!ano_phrase_live(e->st.splitPhraseTag, pos.phrase)) {
        e->st.splitPhraseTag[sp] = pos.phrase;
        e->st.splitPhrases[sp] =
            !e->st.hasModulation
            && policy_of(e, pos.phrase) == ANO_CADENCE_AUTHENTIC
            && e->affect.energy >= 0.6
            && (e->affect.tension >= 0.25
                || (dramaturg_on(e)
                    && ledger_cadence(e, pos.phrase) == ANO_CADENCE_AUTHENTIC)
                || (cfg->form.periods
                    && ano_planner_role(&e->st.planner, pos.phrase) == ANO_FORM_CONSEQUENT));
    }
    return e->st.splitPhrases[sp];
}

static bool wants_64(AnoMusicEngine *e, AnoPhrasePos pos)
{
    const AnoEngineConfig *cfg = &e->config;
    if (!cfg->form.cadential64 || pos.bars < 4 || pos.pos != pos.bars - 3
        || e->st.hasModulation)
        return false;
    if (wants_split(e, pos))
        return false; // D3 owns this phrase's cadence approach
    if (policy_of(e, pos.phrase) != ANO_CADENCE_AUTHENTIC)
        return false;
    if (dramaturg_on(e) && ledger_cadence(e, pos.phrase) == ANO_CADENCE_AUTHENTIC)
        return true;
    if (cfg->form.periods
        && ano_planner_role(&e->st.planner, pos.phrase) == ANO_FORM_CONSEQUENT)
        return true;
    return e->affect.tension >= 0.25;
}

static bool wants_doubling(const AnoMusicEngine *e, AnoTexture texture)
{
    if (!e->config.texture.doubling)
        return false;
    if (texture != ANO_TEX_NONE)
        return texture == ANO_TEX_DOUBLED;
    return e->affect.valence > 0.3 && e->affect.energy > 0.55;
}

static int ladder_index(AnoTexture t)
{
    for (int i = 0; i < 5; ++i)
        if (TEXTURES[i] == t)
            return i;
    return 0;
}

static uint32_t texture_pool(const AnoMusicEngine *e, AnoTexture out[5])
{
    uint32_t n = 0;
    out[n++] = ANO_TEX_MONOPHONIC;
    out[n++] = ANO_TEX_HOMOPHONIC;
    if (e->config.texture.doubling)
        out[n++] = ANO_TEX_DOUBLED;
    if (e->config.texture.imitation)
        out[n++] = ANO_TEX_IMITATIVE;
    if (e->config.texture.counter)
        out[n++] = ANO_TEX_COUNTER;
    return n;
}

static AnoTexture texture_base(const AnoMusicEngine *e, const AnoTexture *pool, uint32_t pn)
{
    double target = (e->affect.energy - 0.15) / 0.75 * 4;
    if (e->affect.valence > 0.3)
        target += 0.5;
    if (target < 0.0)
        target = 0.0;
    if (target > 4.0)
        target = 4.0;
    // min by (|index - target|, index): first strict minimum
    AnoTexture best = pool[0];
    double bestAbs = fabs(ladder_index(pool[0]) - target);
    int bestIdx = ladder_index(pool[0]);
    for (uint32_t i = 1; i < pn; ++i) {
        int idx = ladder_index(pool[i]);
        double a = fabs(idx - target);
        if (a < bestAbs || (a == bestAbs && idx < bestIdx)) {
            best = pool[i];
            bestAbs = a;
            bestIdx = idx;
        }
    }
    return best;
}

static AnoTexture texture_for(AnoMusicEngine *e, AnoPhrasePos pos,
                              const AnoDirective *directive)
{
    AnoConductorState *st = &e->st;
    int phrase = pos.phrase;
    if (phrase < 0)
        return ANO_TEX_HOMOPHONIC;
    if (ano_phrase_live(st->textureTag, phrase))
        return (AnoTexture)st->phraseTextures[ano_ring_phrase(phrase)];
    AnoTexture pool[5];
    uint32_t pn = texture_pool(e, pool);
    AnoTexture prev = ano_phrase_live(st->textureTag, phrase - 1)
                          ? (AnoTexture)st->phraseTextures[ano_ring_phrase(phrase - 1)]
                          : ANO_TEX_NONE;
    AnoTexture prev2 = ano_phrase_live(st->textureTag, phrase - 2)
                           ? (AnoTexture)st->phraseTextures[ano_ring_phrase(phrase - 2)]
                           : ANO_TEX_NONE;
    AnoTexture tex;
    if (e->overrides.hasTexture) {
        tex = e->overrides.texture;
    } else if (pos.kind == ANO_SEG_CODETTA) {
        tex = prev != ANO_TEX_NONE ? prev : ANO_TEX_HOMOPHONIC;
    } else if (directive && directive->payoff > 0) {
        tex = pool[pn - 1]; // the richest (pool is in ladder order)
    } else if (directive && directive->withholdRootTonic) {
        tex = ANO_TEX_HOMOPHONIC;
    } else if (e->config.form.periods
               && ano_planner_role(&st->planner, phrase) == ANO_FORM_CONSEQUENT
               && prev != ANO_TEX_NONE) {
        tex = prev;
    } else {
        AnoTexture base = texture_base(e, pool, pn);
        AnoMusicRng r;
        eng_stream2(e, &r, "texture", phrase);
        bool prev2InPool = false;
        for (uint32_t i = 0; i < pn; ++i)
            if (pool[i] == prev2)
                prev2InPool = true;
        if (prev2 != ANO_TEX_NONE && prev2 != prev && prev2 != base && prev2InPool
            && ano_music_random(&r) < TEXTURE_RETURN_PROB) {
            tex = prev2;
        } else if (base != prev) {
            tex = base;
        } else {
            int idx = ladder_index(base);
            // first of pool minus prev, sorted by (|i - idx|, i)
            bool have = false;
            double bestAbs = 0.0;
            int bestIdx = 0;
            AnoTexture bestT = base;
            for (uint32_t i = 0; i < pn; ++i) {
                if (pool[i] == prev)
                    continue;
                int ti = ladder_index(pool[i]);
                double a = ti - idx < 0 ? idx - ti : ti - idx;
                if (!have || a < bestAbs || (a == bestAbs && ti < bestIdx)) {
                    have = true;
                    bestAbs = a;
                    bestIdx = ti;
                    bestT = pool[i];
                }
            }
            tex = have ? bestT : base;
        }
    }
    st->textureTag[ano_ring_phrase(phrase)] = phrase;
    st->phraseTextures[ano_ring_phrase(phrase)] = (uint8_t)tex;
    return tex;
}

static int tonicize_target(AnoMusicEngine *e, AnoPhrasePos pos)
{
    if (!(dramaturg_on(e) && e->config.dramaturg.earnedDissonance
          && ano_phrase_slot(pos) == ANO_SLOT_PRE_CADENCE))
        return 0;
    const AnoLedger *ledger = &e->st.ledger;
    if (ledger->lament)
        return 0;
    if (ledger_cadence(e, pos.phrase) != ANO_CADENCE_DECEPTIVE)
        return 0;
    int div = e->config.dramaturg.escalatePhrases > 1 ? e->config.dramaturg.escalatePhrases : 1;
    if (ledger->withholdingPhrases / div < 1)
        return 0;
    int target = 6; // CADENCE_TARGET["deceptive"]
    AnoChordQuality q = ano_chord_quality(ano_chord(target, 0),
                                          (AnoScale){ 0, (uint8_t)e->st.currentMode });
    if (q != ANO_QUAL_MAJ && q != ANO_QUAL_MIN)
        return 0;
    return target;
}

// B4 greedy stepwise-bass bias at free bars.
static AnoChord plan_inversion(AnoMusicEngine *e, AnoChord chord, AnoPhrasePos pos, int bar)
{
    AnoConductorState *st = &e->st;
    bool eligible = e->config.form.bassInversions
                 && ano_phrase_slot(pos) == ANO_SLOT_FREE && bar > 0
                 && !chord.applied && chord.inversion == 0
                 && !(chord.extensions & (ANO_EXT_SUS2 | ANO_EXT_SUS4))
                 && !st->hasModulation && st->prevChord.valid;
    if (!eligible) {
        st->inversionRun = chord.inversion ? st->inversionRun + 1 : 0;
        return chord;
    }
    AnoScale scale = { (uint8_t)tonic_for_bar(e, bar), (uint8_t)st->currentMode };
    int prevPc = ano_chord_bass_pc(st->prevChord, scale);
    double scores[2];
    for (int inv = 0; inv < 2; ++inv) {
        AnoChord c = chord;
        c.inversion = (uint8_t)inv;
        int pc = ano_chord_bass_pc(c, scale);
        int d1 = ((pc - prevPc) % 12 + 12) % 12;
        int d2 = ((prevPc - pc) % 12 + 12) % 12;
        int d = d1 < d2 ? d1 : d2;
        double s = d == 0 ? 1.2 : (d == 1 || d == 2) ? 0.0 : 2.0;
        if (inv) {
            s += 0.8;
            if (st->inversionRun >= 2)
                s += 5.0;
        }
        scores[inv] = s;
    }
    int best = scores[0] <= scores[1] ? 0 : 1; // min((0,1)): first minimum
    if (best)
        chord.inversion = 1;
    st->inversionRun = best ? st->inversionRun + 1 : 0;
    return chord;
}

// One bar of symbolic harmony (the lookahead generator).
static AnoChord gen_chord(AnoMusicEngine *e, int bar)
{
    const AnoEngineConfig *cfg = &e->config;
    AnoConductorState *st = &e->st;
    AnoPhrasePos pos = pos_of(e, bar);

    if (!st->hasModulation && st->hasPendingKey) {
        int target = st->pendingKeyPc;
        bool urgent = st->pendingKeyUrgent;
        if (target == st->keyTonic) {
            st->hasPendingKey = false;
        } else if (bar >= 1 && (urgent || pos.pos == pos.bars - 3)) {
            st->hasPendingKey = false;
            plan_modulation(e, bar, target, !urgent);
        }
    }
    if (st->hasModulation
        && (bar == st->modulation.pivotBar || bar == st->modulation.dominantBar
            || bar == st->modulation.arrivalBar)) {
        AnoChord chord;
        if (modulation_chord(e, bar, pos, &chord)) {
            st->prevChord = chord;
            return chord;
        } // else: direct plan, the pivot bar walks normally
    }

    // D2 elision: the shared bar sounds the RESOLUTION
    if (ano_bar_live(st->elisionTag, bar)) {
        AnoChord chord = ano_chord(1, 0);
        st->prevChord = chord;
        return chord;
    }

    // D2 codetta: tonic prolongation, at most a plagal glance
    if (pos.kind == ANO_SEG_CODETTA) {
        bool plagal = false;
        if (pos.pos == 0 && pos.bars > 1) {
            AnoMusicRng r;
            eng_stream2(e, &r, "codetta", bar);
            plagal = ano_music_random(&r) < 0.5;
        }
        AnoChord chord = ano_chord(plagal ? 4 : 1, 0);
        st->prevChord = chord;
        return chord;
    }

    // B2: the consequent opens on the antecedent's harmony
    if (cfg->form.periods && pos.pos == 0
        && ano_planner_role(&st->planner, pos.phrase) == ANO_FORM_CONSEQUENT) {
        int ante = pos.phrase - 1;
        if (ano_planner_has_opening(&st->planner, ante)) {
            AnoChord opening = st->planner.openingChord[ano_ring_phrase(ante)];
            st->prevChord = opening;
            return opening;
        }
    }

    // D3: the compressed 6/4 〜 I64 downbeat, V at the mid-bar pulse
    if (ano_phrase_slot(pos) == ANO_SLOT_PRE_CADENCE && wants_split(e, pos)) {
        AnoChord chord = ano_chord(1, 0);
        chord.inversion = 2;
        if (bar >= 0) {
            st->splitTag[ano_ring_bar(bar)] = bar;
            st->splits[ano_ring_bar(bar)] = ano_chord(5, 0);
        }
        st->prevChord = ano_chord(5, 0); // the cadence continues from the sounding V
        return chord;
    }

    // B1: the cadential 6/4
    if (wants_64(e, pos)) {
        AnoChord chord = ano_chord(1, 0);
        chord.inversion = 2;
        st->prevChord = chord;
        return chord;
    }

    // B4: the lament ground
    AnoCadenceSlot slot = ano_phrase_slot(pos);
    if (dramaturg_on(e) && st->ledger.lament
        && (slot == ANO_SLOT_OPEN || slot == ANO_SLOT_FREE) && bar > 0) {
        AnoChord chord = ano_chord(LAMENT_CYCLE[pos.pos % 4][0], 0);
        chord.inversion = (uint8_t)LAMENT_CYCLE[pos.pos % 4][1];
        st->prevChord = chord;
        if (bar >= 0) {
            st->lamentTag[ano_ring_bar(bar)] = bar;
            st->lamentBars[ano_ring_bar(bar)] = true;
        }
        return chord;
    }

    bool held = harmonic_rhythm(e) == 0.5 && slot == ANO_SLOT_FREE && bar % 2 == 1
             && st->prevChord.valid;
    if (held)
        return st->prevChord;

    AnoChord prev = st->prevChord;
    AnoMusicRng r;
    eng_stream2(e, &r, "harmony", bar);
    AnoChord chord = ano_next_chord(
        prev, slot, (AnoCadencePolicy)policy_of(e, pos.phrase),
        ano_effective_tension(e->affect.tension, pos), e->affect.valence,
        (AnoMode)st->currentMode, pos.pos == 0, bar == 0, &cfg->harmony, &r,
        dramaturg_on(e) && st->ledger.suppressTonic, tonicize_target(e, pos),
        (prev.valid && prev.degree == 1 && prev.inversion == 2)
            || (dramaturg_on(e) && st->ledger.lament));
    chord = plan_inversion(e, chord, pos, bar);
    st->prevChord = chord;
    return chord;
}

static AnoGenParams mapped_params(AnoMusicEngine *e, int bar, double rit,
                                  AnoBarResult *out)
{
    const AnoEngineConfig *cfg = &e->config;
    AnoConductorState *st = &e->st;
    const AnoMappingTable *table = &cfg->mapper;
    const AnoOverrides *ov = &e->overrides;
    AnoAffect a = e->affect;

    if (st->bar == 0 && st->chordQueueLen == 0) { // first bar: snap, don't slew
        st->currentTempo = ov->hasTempoBpm ? ov->tempoBpm : ano_map_tempo(a, table);
        st->currentVelocity = ov->hasVelocityCenter ? ov->velocityCenter
                                                    : ano_map_velocity(a, table);
        st->currentArticulation = ov->hasArticulation ? ov->articulation
                                                      : ano_map_articulation(a, table);
        st->activeLayerCount = ano_gate_layers(NULL, 0, a.energy, table, st->activeLayers);
    }
    st->currentVelocity = ano_music_slew(
        st->currentVelocity,
        ov->hasVelocityCenter ? ov->velocityCenter : ano_map_velocity(a, table),
        table->velocitySlewPerBar);
    st->currentArticulation = ano_music_slew(
        st->currentArticulation,
        ov->hasArticulation ? ov->articulation : ano_map_articulation(a, table),
        table->articulationSlewPerBar);
    if (ov->hasLayers) {
        memcpy(st->activeLayers, ov->layers, sizeof ov->layers);
        st->activeLayerCount = ov->layerCount;
    } else {
        uint8_t next[ANO_MUSIC_LAYER_COUNT];
        uint32_t n = ano_gate_layers(st->activeLayers, st->activeLayerCount,
                                     a.energy, table, next);
        memcpy(st->activeLayers, next, sizeof next);
        st->activeLayerCount = n;
    }

    double tempoGoal = ov->hasTempoBpm ? ov->tempoBpm : ano_map_tempo(a, table);
    double barQ = ano_meter_bar_quarters(cfg->meter);
    int beats = (int)barQ;
    if (beats < 1)
        beats = 1;
    for (int beat = 0; beat < beats; ++beat) {
        st->currentTempo = ano_music_slew(st->currentTempo, tempoGoal,
                                          table->tempoSlewPerBeat);
        double shade = beats > 1 ? (double)beat / (double)(beats - 1) : 1.0;
        double emitted = st->currentTempo * (1.0 - rit * shade);
        bool changed = !st->hasLastEmittedTempo
                    || fabs(emitted - st->lastEmittedTempo) > 0.01;
        if (changed && out->tempoPointCount < 8u) {
            out->tempoPoints[out->tempoPointCount].beat = bar * barQ + beat;
            out->tempoPoints[out->tempoPointCount].bpm = ano_music_round(emitted, 2);
            out->tempoPointCount++;
            st->hasLastEmittedTempo = true;
            st->lastEmittedTempo = emitted;
        }
    }

    AnoGenParams p = cfg->params; // texture/instrument shape; fields overwritten
    p.tempoBpm = ano_music_round(st->currentTempo, 2);
    p.noteDensity = ov->hasNoteDensity ? ov->noteDensity : ano_map_density(a, table);
    p.roughness = ov->hasRoughness ? ov->roughness : ano_map_roughness(a, table);
    p.articulation = ano_music_round(st->currentArticulation, 3);
    p.velocityCenter = (int)ano_music_round_int(st->currentVelocity);
    p.accentDepth = ov->hasAccentDepth ? ov->accentDepth : ano_map_accent(a, table);
    p.registerCenter = ov->hasRegisterCenter ? ov->registerCenter
                                             : ano_map_register(a, table);
    memcpy(p.layers, st->activeLayers, sizeof p.layers);
    p.layerCount = st->activeLayerCount;
    p.harmonicRhythm = harmonic_rhythm(e);
    p.dissonanceBudget = a.tension;
    p.cadencePolicy = policy_of(e, pos_of(e, bar).phrase);
    p.texture = ANO_TEX_NONE;
    p.filterCutoff = ov->hasFilterCutoff ? ov->filterCutoff : ano_map_filter_cutoff(a, table);
    p.reverbSend = ov->hasReverbSend ? ov->reverbSend : ano_map_reverb_send(a, table);
    p.delaySend = ov->hasDelaySend ? ov->delaySend : ano_map_delay_send(a, table);
    p.drive = ov->hasDrive ? ov->drive : ano_map_drive(a, table);
    p.stereoWidth = ov->hasStereoWidth ? ov->stereoWidth : ano_map_stereo_width(a, table);
    memcpy(p.instruments, st->currentInstruments, sizeof p.instruments);
    return p;
}

static void escalate(AnoGenParams *p, double intensify)
{
    int vc = p->velocityCenter + (int)ano_music_round_int(intensify * 14);
    p->velocityCenter = vc < 120 ? vc : 120;
    double nd = p->noteDensity + intensify * 0.20;
    p->noteDensity = nd < 1.0 ? nd : 1.0;
    p->accentDepth = p->accentDepth + (int)ano_music_round_int(intensify * 4);
}

static const AnoMotif *motif_of(AnoMusicEngine *e, int phrase, const AnoGenParams *params)
{
    if (e->config.form.periods
        && ano_planner_role(&e->st.planner, phrase) == ANO_FORM_CONSEQUENT)
        phrase -= 1; // B2: the answer develops the question's material
    if (phrase < 0)
        phrase = 0;
    uint32_t sl = ano_ring_phrase(phrase);
    if (!ano_phrase_live(e->st.motifTag, phrase)) {
        AnoMusicRng r;
        eng_stream2(e, &r, "motif", phrase);
        e->st.motifs[sl] = ano_make_motif(&r, params->noteDensity, params->roughness,
                                          &e->config.melody,
                                          ano_meter_slots(e->config.meter));
        e->st.motifTag[sl] = phrase;
    }
    return &e->st.motifs[sl];
}

/* Engine API */

void ano_engine_init(AnoMusicEngine *e, uint64_t seed, const AnoEngineConfig *cfg)
{
    memset(e, 0, sizeof *e);
    e->config = *cfg;
    e->seed = seed;
    AnoConductorState *st = &e->st;
    st->keyTonic = cfg->keyTonic;
    st->clock = ano_phrase_clock(cfg->phraseBars);
    st->prevBassRoot = ANO_NEAR_NONE;
    st->padTie = ANO_NEAR_NONE;
    st->melody = ano_melody_state_init();
    st->counter = ano_counter_state_init();
    ano_ledger_init(&st->ledger);
    ano_planner_init(&st->planner);
    // tags not values: zero would claim phrase/bar 0
    for (uint32_t i = 0; i < ANO_PHRASE_WINDOW; ++i) {
        st->motifTag[i] = st->grooveTag[i] = st->arpSkipTag[i] = ANO_SLOT_EMPTY;
        st->apexTag[i] = st->imitationTag[i] = st->textureTag[i] = ANO_SLOT_EMPTY;
        st->splitPhraseTag[i] = st->policyTag[i] = ANO_SLOT_EMPTY;
        st->phrasePolicies[i] = ANO_CADENCE_NONE;
    }
    for (uint32_t i = 0; i < ANO_BAR_WINDOW; ++i) {
        st->elisionTag[i] = st->lamentTag[i] = st->splitTag[i] = ANO_SLOT_EMPTY;
        st->elisions[i] = -1;
    }
    e->affect = ano_affect_clamped((AnoAffect){ cfg->valence, cfg->energy, cfg->tension });
    if (cfg->motifLibraryCount)
        ano_director_init(&st->director, cfg->motifLibrary, cfg->motifLibraryCount);
    if (cfg->hasMapper) {
        st->currentMode = cfg->mode != ANO_MODE_NONE
                        ? cfg->mode : (int)ano_nearest_mode(e->affect.valence);
    } else {
        st->currentMode = cfg->mode != ANO_MODE_NONE ? cfg->mode : ANO_MODE_IONIAN;
        st->currentTempo = cfg->params.tempoBpm;
    }
    e->scale = (AnoScale){ (uint8_t)cfg->keyTonic, (uint8_t)st->currentMode };
}

void ano_engine_set_affect(AnoMusicEngine *e, double valence, double energy,
                           double tension, bool urgent)
{
    AnoAffect a = e->affect;
    if (!isnan(valence))
        a.valence = valence;
    if (!isnan(energy))
        a.energy = energy;
    if (!isnan(tension))
        a.tension = tension;
    e->affect = ano_affect_clamped(a);
    if (urgent)
        e->urgent = true;
}

void ano_engine_request_key(AnoMusicEngine *e, int pc, bool urgent)
{
    e->st.hasPendingKey = true;
    e->st.pendingKeyPc = (pc % 12 + 12) % 12;
    e->st.pendingKeyUrgent = urgent;
}

void ano_engine_request_motif(AnoMusicEngine *e, const char *tag)
{
    strncpy(e->st.requestedMotif, tag, sizeof e->st.requestedMotif - 1);
    e->st.requestedMotif[sizeof e->st.requestedMotif - 1] = '\0';
}

// stable sort by (start, pitch)
static void sort_events(AnoMusicEvent *ev, uint32_t n)
{
    for (uint32_t i = 1; i < n; ++i) {
        AnoMusicEvent key = ev[i];
        uint32_t j = i;
        while (j > 0
               && (ev[j - 1].core.start > key.core.start
                   || (ev[j - 1].core.start == key.core.start
                       && ev[j - 1].core.pitch > key.core.pitch))) {
            ev[j] = ev[j - 1];
            --j;
        }
        ev[j] = key;
    }
}

void ano_engine_advance_bar(AnoMusicEngine *e, AnoBarResult *out)
{
    const AnoEngineConfig *cfg = &e->config;
    AnoConductorState *st = &e->st;
    memset(out, 0, sizeof *out);
    int bar = st->bar;
    out->bar = bar;
    AnoPhrasePos pos = pos_of(e, bar);
    double barQ = ano_meter_bar_quarters(cfg->meter);

    // D2 extension: a hot withhold stretches this phrase's pre-dominant
    if (cfg->clock.extension && pos.pos == 0 && pos.kind == ANO_SEG_REGULAR && bar > 0
        && dramaturg_on(e) && st->ledger.withholdingPhrases > 0
        && e->affect.tension >= (cfg->clock.extensionTension > cfg->dramaturg.accrueAbove
                                 ? cfg->clock.extensionTension : cfg->dramaturg.accrueAbove)
        && ano_planner_role(&st->planner, pos.phrase) == ANO_FORM_NONE
        && (int)st->clock.segmentCount <= pos.phrase
        && !st->hasModulation && !st->hasPendingKey) {
        AnoMusicRng r;
        eng_stream2(e, &r, "clock", pos.phrase);
        if (ano_music_random(&r) < 0.6) {
            ano_clock_schedule(&st->clock, pos.phrase, cfg->phraseBars + 2,
                               ANO_SEG_EXTENSION, 0);
            pos = pos_of(e, bar);
        }
    }

    // Dramaturg: the phrase's cadence rationing, decided at pos 0
    AnoDirective directiveStore, *directive = NULL;
    if (dramaturg_on(e) && pos.kind != ANO_SEG_CODETTA) {
        directiveStore = ano_dramaturg_on_bar(&cfg->dramaturg, &st->ledger,
                                              e->affect.tension, pos);
        directive = &directiveStore;
    }

    // D2 codetta / elision
    if (pos.pos == 0 && pos.kind == ANO_SEG_REGULAR
        && (cfg->clock.codetta || cfg->clock.elision)
        && !st->hasModulation && !st->hasPendingKey
        && (int)st->clock.segmentCount <= pos.phrase) {
        if (cfg->clock.codetta && directive && directive->payoff >= cfg->clock.codettaPayoff) {
            ano_clock_materialize_through(&st->clock, pos.phrase);
            ano_clock_schedule(&st->clock, pos.phrase + 1, cfg->clock.codettaBars,
                               ANO_SEG_CODETTA, 0);
        } else if (cfg->clock.elision && e->affect.energy >= cfg->clock.elisionEnergy
                   && policy_of(e, pos.phrase) == ANO_CADENCE_AUTHENTIC
                   && !(directive && directive->payoff > 0)
                   && ano_planner_role(&st->planner, pos.phrase) == ANO_FORM_NONE) {
            AnoMusicRng r;
            eng_stream2(e, &r, "clock", pos.phrase);
            if (ano_music_random(&r) < 0.5) {
                ano_clock_materialize_through(&st->clock, pos.phrase);
                AnoSegment seg = ano_clock_schedule(&st->clock, pos.phrase + 1,
                                                    cfg->phraseBars, ANO_SEG_ELISION, 1);
                if (seg.start >= 0) {
                    st->elisionTag[ano_ring_bar(seg.start)] = seg.start;
                    st->elisions[ano_ring_bar(seg.start)] = pos.phrase;
                }
            }
        }
    }

    if (cfg->wanderPhrases >= 0 && pos.pos == 0 && bar > 0
        && pos.kind != ANO_SEG_CODETTA && !st->hasPendingKey && !st->hasModulation
        && pos.phrase - st->lastKeyPhrase >= cfg->wanderPhrases) {
        st->hasPendingKey = true;
        st->pendingKeyPc = wander_target(e, pos.phrase);
        st->pendingKeyUrgent = false;
    }

    // B2 period commitment at even phrase boundaries
    if (cfg->form.periods && pos.pos == 0 && pos.kind == ANO_SEG_REGULAR) {
        if (pos.phrase % 2 == 0
            && ano_planner_role(&st->planner, pos.phrase) == ANO_FORM_NONE
            && !st->hasModulation && !st->hasPendingKey
            && (int)st->clock.segmentCount <= pos.phrase + 1
            && !(dramaturg_on(e) && ledger_cadence(e, pos.phrase) != ANO_CADENCE_NONE)) {
            AnoMusicRng r;
            eng_stream2(e, &r, "period", pos.phrase);
            if (ano_music_random(&r) < cfg->form.periodProb)
                ano_planner_commit(&st->planner, pos.phrase);
        }
    }

    // instrument swaps (phrase-quantized, urgent demotes; read urgent BEFORE
    // the mode block clears it)
    if (cfg->hasMapper && pos.kind != ANO_SEG_CODETTA && (pos.pos == 0 || e->urgent)) {
        uint8_t picked[ANO_MUSIC_LAYER_COUNT];
        if (e->overrides.hasInstruments)
            memcpy(picked, e->overrides.instruments, sizeof picked);
        else
            ano_pick_instruments(st->currentInstruments, e->affect.energy,
                                 &cfg->mapper, picked);
        if (memcmp(picked, st->currentInstruments, sizeof picked) != 0)
            memcpy(st->currentInstruments, picked, sizeof picked);
    }

    // the mode holds through a modulation window; codettas hold everything
    if (cfg->hasMapper && pos.kind != ANO_SEG_CODETTA
        && (pos.pos == 0 || e->urgent) && !st->hasModulation) {
        int pinned = e->overrides.hasMode ? e->overrides.mode : cfg->mode;
        st->currentMode = pinned != ANO_MODE_NONE
                        ? pinned
                        : (int)ano_pick_mode((AnoMode)st->currentMode,
                                             e->affect.valence, &cfg->mapper);
        if (directive && directive->brighten && pinned == ANO_MODE_NONE)
            st->currentMode = (int)ano_brighter_mode((AnoMode)st->currentMode,
                                                     directive->brighten);
        e->urgent = false;
    }
    e->scale = (AnoScale){ (uint8_t)tonic_for_bar(e, bar), (uint8_t)st->currentMode };

    double rit = rit_depth(e, pos);
    AnoGenParams params;
    if (cfg->hasMapper) {
        params = mapped_params(e, bar, rit, out);
    } else {
        params = cfg->params;
        if (bar == 0) {
            out->tempoPoints[out->tempoPointCount].beat = 0.0;
            out->tempoPoints[out->tempoPointCount].bpm = params.tempoBpm;
            out->tempoPointCount++;
        }
        if (st->hasTempoRestore) { // a tempo after last bar's rit
            out->tempoPoints[out->tempoPointCount].beat = bar * barQ;
            out->tempoPoints[out->tempoPointCount].bpm = st->tempoRestore;
            out->tempoPointCount++;
            st->hasTempoRestore = false;
        }
        if (rit != 0.0) {
            int beats = (int)barQ;
            if (beats < 2)
                beats = 2;
            for (int beat = 1; beat < beats && out->tempoPointCount < 8u; ++beat) {
                out->tempoPoints[out->tempoPointCount].beat = bar * barQ + beat;
                out->tempoPoints[out->tempoPointCount].bpm = ano_music_round(
                    params.tempoBpm * (1.0 - rit * beat / (beats - 1)), 2);
                out->tempoPointCount++;
            }
            st->hasTempoRestore = true;
            st->tempoRestore = params.tempoBpm;
        }
    }

    // dramaturg withholding: locked tiers + escalation
    if (directive) {
        if (directive->lockLayerCount) {
            uint8_t kept[ANO_MUSIC_LAYER_COUNT];
            uint32_t n = 0;
            for (uint32_t i = 0; i < params.layerCount; ++i)
                if (!layers_has(directive->lockLayers, directive->lockLayerCount,
                                params.layers[i]))
                    kept[n++] = params.layers[i];
            memcpy(params.layers, kept, sizeof params.layers);
            params.layerCount = n;
        }
        if (directive->intensify != 0.0)
            escalate(&params, directive->intensify);
    }

    // B3 hypermetric weight
    if (cfg->form.hypermeter) {
        double hyper = ano_hyper_weight(pos.pos, pos.bars);
        int vc = params.velocityCenter + (int)ano_music_round_int(6 * (hyper - 0.7));
        params.velocityCenter = vc < 1 ? 1 : vc > 127 ? 127 : vc;
    }

    // C4 texture rotation
    if (cfg->texture.rotate || e->overrides.hasTexture)
        params.texture = texture_for(e, pos, directive);

    // C5: the countermelody joins as a texture state
    if (cfg->texture.counter
        && layers_has(params.layers, params.layerCount, ANO_MUSIC_MELODY)
        && !layers_has(params.layers, params.layerCount, ANO_MUSIC_COUNTER)
        && pos.kind != ANO_SEG_CODETTA
        && ((cfg->texture.rotate || params.texture != ANO_TEX_NONE)
                ? params.texture == ANO_TEX_COUNTER
                : e->affect.energy > 0.45)) {
        params.layers[params.layerCount++] = ANO_MUSIC_COUNTER;
    }

    // M15 lifecycle: built lazily from LIVE params; advances per phrase
    AnoMelodyStage lifecycleState = ANO_MEL_PLAIN;
    if (lifecycle_on(e)) {
        if (!st->hasLifecycle) {
            AnoMusicRng r;
            eng_stream1(e, &r, "signature");
            st->lifecycle = (AnoMotifLifecycle){
                .motif = ano_make_signature(&r, params.noteDensity, params.roughness,
                                            &cfg->melody, ano_meter_slots(cfg->meter), 8),
                .developAfter = 2, .completedPhrase = -1,
            };
            st->hasLifecycle = true;
        }
        if (pos.pos == 0 && pos.kind != ANO_SEG_CODETTA)
            ano_lifecycle_advance(&st->lifecycle, directive && directive->payoff > 0,
                                  pos.phrase);
        lifecycleState = st->lifecycle.state == ANO_LIFE_COMPLETED ? ANO_MEL_COMPLETED
                       : st->lifecycle.state == ANO_LIFE_DEVELOPED ? ANO_MEL_DEVELOPED
                                                                   : ANO_MEL_INTRODUCED;
    }

    // A4 apex, drawn once per phrase from the phrase-start params
    const AnoApexPlan *apex = NULL;
    if (cfg->melody.planApex && pos.kind != ANO_SEG_CODETTA && pos.phrase >= 0) {
        uint32_t ap = ano_ring_phrase(pos.phrase);
        if (!ano_phrase_live(st->apexTag, pos.phrase)) {
            AnoMusicRng r;
            eng_stream2(e, &r, "apex", pos.phrase);
            st->apexes[ap] = ano_make_apex(&r, pos.bars, params.registerCenter,
                                           cfg->melody.rangeSemitones);
            st->apexTag[ap] = pos.phrase;
        }
        apex = &st->apexes[ap];
    }

    // the one-bar chord lookahead
    while (st->chordQueueLen < 2) {
        int nextNeeded = st->chordQueueLen ? st->chordQueue[st->chordQueueLen - 1].bar + 1
                                           : bar;
        st->chordQueue[st->chordQueueLen].bar = nextNeeded;
        st->chordQueue[st->chordQueueLen].chord = gen_chord(e, nextNeeded);
        st->chordQueueLen++;
    }
    AnoChord chord = st->chordQueue[0].chord;
    st->chordQueue[0] = st->chordQueue[1];
    st->chordQueueLen--;
    AnoChord upcoming = st->chordQueue[0].chord;
    AnoScale nextScale = { (uint8_t)tonic_for_bar(e, bar + 1), (uint8_t)st->currentMode };

    AnoCtxSlot ctxSlot = ano_phrase_slot(pos) == ANO_SLOT_PRE_CADENCE ? ANO_CTX_SLOT_PRE_CADENCE
                       : ano_phrase_slot(pos) == ANO_SLOT_CADENCE ? ANO_CTX_SLOT_CADENCE
                                                                  : ANO_CTX_SLOT_NONE;
    if (pos.kind == ANO_SEG_CODETTA)
        ctxSlot = ANO_CTX_SLOT_NONE;
    bool inModWindow = st->hasModulation
                    && (bar == st->modulation.pivotBar || bar == st->modulation.dominantBar
                        || bar == st->modulation.arrivalBar);
    if (inModWindow && !st->modulation.hasCadencePhrase)
        ctxSlot = ANO_CTX_SLOT_NONE; // an urgent window disarms overlapped slots

    AnoHarmonicContext ctx = { 0 };
    ctx.bar = bar;
    ctx.scale = e->scale;
    ctx.chord = chord;
    ano_chord_symbol(chord, e->scale, ctx.chordSym, sizeof ctx.chordSym);
    ctx.chordPcCount = ano_chord_voiced_pcs(chord, e->scale, ctx.chordPcs);
    ctx.nextChord = upcoming;
    ano_chord_symbol(upcoming, nextScale, ctx.nextChordSym, sizeof ctx.nextChordSym);
    ctx.tension = ano_effective_tension(e->affect.tension, pos);
    ctx.cadenceSlot = ctxSlot;
    ctx.cadencePolicy = ctxSlot != ANO_CTX_SLOT_NONE ? policy_of(e, pos.phrase)
                                                     : ANO_CADENCE_NONE;
    if (inModWindow)
        snprintf(ctx.modulation, sizeof ctx.modulation, "-> tonic %d",
                 st->modulation.targetTonic); // annotation only; content elided
    if (chord.degree == 1 && chord.inversion == 2) {
        ctx.obligation = ANO_OBL_CADENTIAL64;
    } else if (ano_bar_live(st->lamentTag, bar) && st->lamentBars[ano_ring_bar(bar)]
               && st->ledger.lament) {
        ctx.obligation = ANO_OBL_LAMENT;
    } else if (chord.applied) {
        ctx.obligation = ANO_OBL_TONICIZE;
        ctx.obligationTarget = chord.applied;
    }
    ctx.phrasePos = pos.pos;
    ctx.phraseBars = pos.bars;
    ctx.phraseApex = apex ? apex->pos : -1;
    ctx.form = ano_planner_role(&st->planner, pos.phrase);
    if (ano_bar_live(st->splitTag, bar)) {
        ctx.chords[0] = (AnoChordSpan){ 0.0, chord };
        ctx.chords[1] = (AnoChordSpan){ barQ / 2, st->splits[ano_ring_bar(bar)] };
        ctx.chordSpanCount = 2;
    }
    if (ano_bar_live(st->elisionTag, bar)) {
        ctx.cadenceSlot = ANO_CTX_SLOT_CADENCE; // the OLD phrase's promise
        ctx.cadencePolicy = policy_of(e, st->elisions[ano_ring_bar(bar)]);
    }

    AnoMusicEvent *events = out->rawEvents;
    uint32_t nEvents = 0;

    // M17 authored signatures at a phrase boundary
    if (director_on(e) && pos.pos == 0 && pos.kind != ANO_SEG_CODETTA) {
        double leniency = dramaturg_on(e) ? cfg->dramaturg.leniency : cfg->motifLeniency;
        int lo = params.registerCenter - cfg->melody.rangeSemitones;
        int hi = params.registerCenter + cfg->melody.rangeSemitones;
        uint32_t strongMask = 0;
        {
            int ss[ANO_METER_MAX_SLOTS];
            uint32_t sn = ano_meter_strong_slots(cfg->meter, ss);
            for (uint32_t i = 0; i < sn; ++i)
                strongMask |= 1u << ss[i];
        }
        uint32_t sigIdx;
        AnoTransform xf;
        AnoMotif motifT;
        if (ano_director_select(&st->director, e->scale, ctx.chordPcs, ctx.chordPcCount,
                                lo, hi, strongMask, leniency, st->melody.prevPitch,
                                st->requestedMotif, &sigIdx, &xf, &motifT)) {
            const AnoSignatureMotif *sig = &st->director.library[sigIdx];
            st->pendingSignature = motifT;
            st->hasPendingSignature = true;
            st->pendingLifecycle = sig->importance >= LANDMARK_IMPORTANCE
                                 ? ANO_MEL_COMPLETED : ANO_MEL_STATED;
            ano_director_observe(&st->director, sig->tag, pos.bars);
            if (strcmp(sig->tag, st->requestedMotif) == 0)
                st->requestedMotif[0] = '\0';
            if (sig->importance >= LANDMARK_IMPORTANCE && dramaturg_on(e)) {
                // a landmark lands as an arrival: cash the ledger
                AnoLedger *led = &st->ledger;
                led->lastSpend = ano_spend_magnitude(led, &cfg->dramaturg);
                led->barsSinceAuthentic = 0;
                led->deceptions = 0;
                ano_ledger_set_cadence(led, pos.phrase, ANO_CADENCE_AUTHENTIC);
            }
        } else {
            st->hasPendingSignature = false;
            ano_director_age(&st->director, pos.bars);
        }
    }

    AnoMusicEvent bassEvents[8];
    uint32_t bassCount = 0;
    int prevBassRoot = st->prevBassRoot; // A3 cadence yardstick (last bar's)

    // --- pad ---
    if (layers_has(params.layers, params.layerCount, ANO_MUSIC_PAD)
        && ano_bar_live(st->splitTag, bar)) {
        // D3: the split bar re-voices at the mid-bar pulse
        double half = barQ / 2;
        int vel = params.velocityCenter + (-6);
        vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
        uint8_t pcs1[5], pcs2[5];
        uint32_t n1 = ano_chord_pitch_classes(chord, e->scale, pcs1);
        uint32_t n2 = ano_chord_pitch_classes(st->splits[ano_ring_bar(bar)], e->scale,
                                              pcs2);
        // C4 monophonic: split path must thin itself (same dyad rule as generate_pad).
        AnoVoicingConfig vcfg = cfg->voicing;
        if (params.texture == ANO_TEX_MONOPHONIC) {
            n1 = ano_thin_voicing(pcs1, n1, &vcfg); // voices=2 is idempotent,
            n2 = ano_thin_voicing(pcs2, n2, &vcfg); // so both blocks share vcfg
        }
        int v1[6], v2[6];
        uint32_t vn1 = ano_voice_chord(pcs1, n1, st->prevVoicingLen ? st->prevVoicing : NULL,
                                       st->prevVoicingLen, &vcfg, v1, NULL);
        uint32_t vn2 = ano_voice_chord(pcs2, n2, v1, vn1, &vcfg, v2, NULL);
        for (int seg = 0; seg < 2; ++seg) {
            const int *v = seg ? v2 : v1;
            uint32_t vn = seg ? vn2 : vn1;
            for (uint32_t i = 0; i < vn; ++i) {
                AnoMusicEvent *ev = &events[nEvents++];
                *ev = (AnoMusicEvent){ 0 };
                ev->core = (AnoNoteEvent){ bar * barQ + (seg ? half : 0.0), half,
                                           (uint8_t)v[i], (uint8_t)vel, ANO_MUSIC_PAD,
                                           ANO_MUSIC_TIE_NONE };
                int deg = ano_scale_degree_of(e->scale, v[i]);
                ev->degree = deg > 0 ? (uint8_t)deg : 0;
                strncpy(ev->chordSym, ctx.chordSym, sizeof ev->chordSym - 1);
                strncpy(ev->role, ano_scale_contains(e->scale, v[i]) ? "chord-tone"
                                                                     : "borrowed",
                        sizeof ev->role - 1);
            }
        }
        for (uint32_t i = 0; i < vn2; ++i)
            st->prevVoicing[i] = v2[i];
        st->prevVoicingLen = vn2;
        st->padTie = ANO_NEAR_NONE;
    } else if (layers_has(params.layers, params.layerCount, ANO_MUSIC_PAD)) {
        AnoPadAnimate animate = ANO_PAD_BLOCK;
        if (cfg->texture.animate && params.texture != ANO_TEX_MONOPHONIC) {
            bool suspensionZone = dramaturg_on(e)
                               && ledger_cadence(e, pos.phrase) != ANO_CADENCE_NONE
                               && pos.pos >= pos.bars - 3;
            if (!suspensionZone) {
                if (params.noteDensity < 0.40)
                    animate = ANO_PAD_CONNECTIVE;
                else if (params.noteDensity < 0.62)
                    animate = ANO_PAD_COMPING;
            }
        }
        AnoPadTiePrep prep, *prepPtr = NULL;
        if (cfg->ties.suspension && dramaturg_on(e) && cfg->dramaturg.earnedDissonance
            && ledger_cadence(e, pos.phrase) != ANO_CADENCE_NONE
            && pos.pos == pos.bars - 3 && !wants_split(e, pos)) {
            prep.pcCount = ano_chord_pitch_classes(upcoming, nextScale, prep.pcs);
            prep.chordPcCount = ano_chord_voiced_pcs(upcoming, nextScale, prep.chordPcs);
            prep.scale = nextScale;
            prepPtr = &prep;
        }
        uint8_t nextPcs[5];
        uint32_t nextN = 0;
        AnoMusicRng padRng, *padRngPtr = NULL;
        if (animate != ANO_PAD_BLOCK) {
            nextN = ano_chord_pitch_classes(upcoming, nextScale, nextPcs);
            eng_stream2(e, &padRng, "pad", bar);
            padRngPtr = &padRng;
        }
        AnoPadResult pr;
        ano_generate_pad(&ctx, cfg->meter, &params,
                         st->prevVoicingLen ? st->prevVoicing : NULL, st->prevVoicingLen,
                         &cfg->voicing,
                         directive && directive->suspend,
                         directive && directive->appoggiatura,
                         nextN ? nextPcs : NULL, nextN, animate, padRngPtr,
                         params.texture == ANO_TEX_MONOPHONIC, prepPtr, st->padTie, &pr);
        for (uint32_t i = 0; i < pr.eventCount; ++i)
            events[nEvents++] = pr.events[i];
        for (uint32_t i = 0; i < pr.voiceCount; ++i)
            st->prevVoicing[i] = pr.voicing[i];
        st->prevVoicingLen = pr.voiceCount;
        st->padTie = ANO_NEAR_NONE;
        for (uint32_t i = 0; i < pr.eventCount; ++i)
            if (pr.events[i].core.tie == ANO_MUSIC_TIE_OUT
                || pr.events[i].core.tie == ANO_MUSIC_TIE_BOTH) {
                st->padTie = pr.events[i].core.pitch;
                break;
            }
    } else {
        st->padTie = ANO_NEAR_NONE; // a silent pad bar dissolves any pending prep
    }

    // --- bass ---
    if (layers_has(params.layers, params.layerCount, ANO_MUSIC_BASS)) {
        AnoMusicRng r;
        eng_stream2(e, &r, "bass", bar);
        AnoBassResult br;
        ano_generate_bass(&ctx, cfg->meter, &params, st->prevBassRoot,
                          ano_chord_bass_pc(upcoming, nextScale), &cfg->bass, &r,
                          directive ? directive->pedal : 0, &br);
        for (uint32_t i = 0; i < br.eventCount; ++i) {
            events[nEvents++] = br.events[i];
            bassEvents[bassCount++] = br.events[i];
        }
        st->prevBassRoot = br.root;
    }

    // --- melody ---
    AnoMusicEvent melEvents[ANO_MELODY_MAX_EVENTS];
    uint32_t melCount = 0;
    bool melodyOn = layers_has(params.layers, params.layerCount, ANO_MUSIC_MELODY);
    if (melodyOn && pos.kind == ANO_SEG_CODETTA) {
        // D2 codetta: echo the cadence tail an octave up, then rest
        if (pos.pos == 0 && st->cadenceTailLen) {
            int hiWin = params.registerCenter + cfg->melody.rangeSemitones;
            int baseSlot = st->cadenceTail[0].slot;
            for (uint32_t i = 0; i < st->cadenceTailLen; ++i) {
                int p = st->cadenceTail[i].pitch + 12 <= hiWin
                      ? st->cadenceTail[i].pitch + 12 : st->cadenceTail[i].pitch;
                int vel = params.velocityCenter - 6;
                vel = vel < 1 ? 1 : vel;
                AnoMusicEvent *ev = &melEvents[melCount++];
                *ev = (AnoMusicEvent){ 0 };
                ev->core = (AnoNoteEvent){
                    bar * barQ + (st->cadenceTail[i].slot - baseSlot) * ANO_MUSIC_GRID,
                    st->cadenceTail[i].durSlots * ANO_MUSIC_GRID,
                    (uint8_t)p, (uint8_t)vel, ANO_MUSIC_MELODY, ANO_MUSIC_TIE_NONE };
                int deg = ano_scale_degree_of(ctx.scale, p);
                ev->degree = deg > 0 ? (uint8_t)deg : 0;
                strncpy(ev->chordSym, ctx.chordSym, sizeof ev->chordSym - 1);
                strncpy(ev->role, "motif", sizeof ev->role - 1);
            }
        }
        AnoMelodyState ms = ano_melody_state_init();
        ms.prevPitch = melCount ? melEvents[melCount - 1].core.pitch
                                : st->melody.prevPitch;
        ms.prevAnchor = st->melody.prevAnchor;
        st->melody = ms; // prev_outer=None, pending tie consumed
        for (uint32_t i = 0; i < melCount; ++i)
            events[nEvents++] = melEvents[i];
    } else if (melodyOn) {
        AnoMelodyConfig melCfg = cfg->melody;
        if (directive && directive->registerCap) {
            int r = cfg->melody.rangeSemitones - directive->registerCap;
            melCfg.rangeSemitones = r > MIN_MELODY_RANGE ? r : MIN_MELODY_RANGE;
        }
        const AnoMotif *signature = NULL;
        AnoMelodyStage melLifecycle = ANO_MEL_PLAIN;
        if (director_on(e) && st->hasPendingSignature) {
            signature = &st->pendingSignature;
            melLifecycle = st->pendingLifecycle;
        } else if (lifecycleState != ANO_MEL_PLAIN) {
            signature = &st->lifecycle.motif;
            melLifecycle = lifecycleState;
        }
        // B2 verbatim answer when harmony and scale still match
        const AnoPlacedNote *replay = NULL;
        uint32_t replayCount = 0;
        if (cfg->form.periods && pos.pos == 0
            && ano_planner_role(&st->planner, pos.phrase) == ANO_FORM_CONSEQUENT) {
            int ante = pos.phrase - 1;
            uint32_t an = ano_ring_phrase(ante);
            if (ano_planner_has_opening(&st->planner, ante)
                && memcmp(&st->planner.openingChord[an], &chord, sizeof chord) == 0
                && st->planner.openingScale[an].tonic == e->scale.tonic
                && st->planner.openingScale[an].mode == e->scale.mode) {
                replay = st->planner.openingMelody[an];
                replayCount = st->planner.openingMelodyN[an];
            }
        }
        AnoMusicRng melRng, anaRng, syncRng;
        eng_stream2(e, &melRng, "melody", bar);
        AnoMusicRng *anaPtr = NULL, *syncPtr = NULL;
        if (cfg->ties.anacrusis) {
            eng_stream2(e, &anaRng, "pickup", bar);
            anaPtr = &anaRng;
        }
        if (cfg->ties.syncopation) {
            eng_stream2(e, &syncRng, "syncopate", bar);
            syncPtr = &syncRng;
        }
        AnoMelodyResult mr;
        ano_generate_melody(&ctx, cfg->meter, &params, pos,
                            motif_of(e, pos.phrase, &params), &st->melody, &melCfg,
                            &melRng, melLifecycle, signature, apex,
                            bassCount ? bassEvents : NULL, bassCount,
                            replay, replayCount,
                            wants_doubling(e, params.texture), prevBassRoot,
                            anaPtr, syncPtr, &mr);
        melCount = mr.eventCount;
        memcpy(melEvents, mr.events, melCount * sizeof melEvents[0]);
        for (uint32_t i = 0; i < melCount; ++i)
            events[nEvents++] = melEvents[i];
        st->melody = mr.state;
        if (ano_phrase_slot(pos) == ANO_SLOT_CADENCE && melCount) {
            // the cadence gesture's tail, remembered for a codetta echo
            const AnoMusicEvent *surf[ANO_MELODY_MAX_EVENTS];
            uint32_t sn = 0;
            for (uint32_t i = 0; i < melCount; ++i)
                if (strcmp(melEvents[i].role, "doubling") != 0)
                    surf[sn++] = &melEvents[i];
            for (uint32_t i = 1; i < sn; ++i) { // stable by start
                const AnoMusicEvent *key = surf[i];
                uint32_t j = i;
                while (j > 0 && surf[j - 1]->core.start > key->core.start) {
                    surf[j] = surf[j - 1];
                    --j;
                }
                surf[j] = key;
            }
            uint32_t first = sn > 3 ? sn - 3 : 0;
            st->cadenceTailLen = 0;
            for (uint32_t i = first; i < sn; ++i) {
                int d = (int)ano_music_round_int(surf[i]->core.dur / ANO_MUSIC_GRID);
                st->cadenceTail[st->cadenceTailLen++] = (AnoPlacedNote){
                    ano_meter_slot_of(cfg->meter, surf[i]->core.start),
                    d > 1 ? d : 1, surf[i]->core.pitch };
            }
        }
        if (cfg->form.periods && pos.pos == 0
            && ano_planner_role(&st->planner, pos.phrase) == ANO_FORM_ANTECEDENT
            && pos.phrase >= 0) {
            uint32_t op = ano_ring_phrase(pos.phrase);
            st->planner.openingTag[op] = pos.phrase;
            st->planner.openingChord[op] = chord;
            st->planner.openingScale[op] = e->scale;
            uint32_t n = 0;
            for (uint32_t i = 0; i < melCount && n < ANO_MOTIF_MAX; ++i)
                if (strcmp(melEvents[i].role, "doubling") != 0)
                    st->planner.openingMelody[op][n++] = (AnoPlacedNote){
                        ano_meter_slot_of(cfg->meter, melEvents[i].core.start),
                        (int)ano_music_round_int(melEvents[i].core.dur / ANO_MUSIC_GRID),
                        melEvents[i].core.pitch };
            st->planner.openingMelodyN[op] = n;
        }
        // C3 imitation: one entry per phrase, the bar after the statement
        if (cfg->texture.imitation && pos.pos == 1 && pos.kind != ANO_SEG_CODETTA
            && (params.texture == ANO_TEX_NONE || params.texture == ANO_TEX_IMITATIVE)
            && pos.phrase >= 0 && !ano_phrase_live(st->imitationTag, pos.phrase)
            && (layers_has(params.layers, params.layerCount, ANO_MUSIC_ARP)
                || layers_has(params.layers, params.layerCount, ANO_MUSIC_PAD))) {
            int imitLo, imitHi, imitVel;
            uint8_t host;
            if (layers_has(params.layers, params.layerCount, ANO_MUSIC_ARP)) {
                imitLo = (cfg->arp.baseOctave + 1) * 12;
                imitHi = imitLo + cfg->arp.spanOctaves * 12;
                host = ANO_MUSIC_ARP;
                imitVel = params.velocityCenter + cfg->arp.velocityOffset + 6;
            } else {
                imitHi = cfg->voicing.hi;
                imitLo = imitHi - 15;
                host = ANO_MUSIC_PAD;
                imitVel = params.velocityCenter + (-6);
            }
            AnoImitationResult ir;
            ano_generate_imitation(&ctx, cfg->meter,
                                   signature ? signature : motif_of(e, pos.phrase, &params),
                                   melEvents, melCount, host, imitLo, imitHi, imitVel, &ir);
            if (ir.eventCount) {
                st->imitationTag[ano_ring_phrase(pos.phrase)] = pos.phrase;
                st->imitationCells[ano_ring_phrase(pos.phrase)] = ir.emitted;
                for (uint32_t i = 0; i < ir.eventCount; ++i)
                    events[nEvents++] = ir.events[i];
            }
        }
        // C5 counter, after melody and bass
        if (layers_has(params.layers, params.layerCount, ANO_MUSIC_COUNTER)) {
            AnoMusicRng r;
            eng_stream2(e, &r, "counter", bar);
            AnoCounterResult cr;
            ano_generate_counter(&ctx, cfg->meter, &params, melEvents, melCount,
                                 bassEvents, bassCount, &st->counter, &cfg->counter,
                                 &r, &cr);
            for (uint32_t i = 0; i < cr.eventCount; ++i)
                events[nEvents++] = cr.events[i];
            st->counter = cr.state;
        }
    }

    // --- arp ---
    if (layers_has(params.layers, params.layerCount, ANO_MUSIC_ARP)) {
        AnoMusicRng patternRng;
        eng_stream2(e, &patternRng, "arp-pattern", pos.phrase);
        AnoArpPattern pattern = (AnoArpPattern)ano_music_choice(&patternRng,
                                                                ANO_ARP_PATTERN_COUNT);
        bool hasSkips = false;
        uint32_t skips = 0;
        if (cfg->phraseGroove && pos.phrase >= 0) {
            uint32_t sk = ano_ring_phrase(pos.phrase);
            if (!ano_phrase_live(st->arpSkipTag, pos.phrase)) {
                AnoMusicRng r;
                eng_stream2(e, &r, "arp-groove", pos.phrase);
                st->arpSkips[sk] = ano_arp_make_skips(&r, cfg->meter,
                                                      params.noteDensity);
                st->arpSkipTag[sk] = pos.phrase;
            }
            hasSkips = true;
            skips = st->arpSkips[sk];
        }
        AnoMusicRng arpRng;
        eng_stream2(e, &arpRng, "arp", bar);
        AnoArpResult ar;
        ano_generate_arp(&ctx, cfg->meter, &params, pattern, &cfg->arp, &arpRng,
                         hasSkips, skips, &ar);
        // C3 masking: an imitation entry in the arp register wins its span
        for (uint32_t i = 0; i < ar.eventCount; ++i) {
            bool masked = false;
            for (uint32_t j = 0; j < nEvents && !masked; ++j) {
                const AnoMusicEvent *im = &events[j];
                if (im->core.layer != ANO_MUSIC_ARP || strcmp(im->role, "imitation") != 0)
                    continue;
                double aEnd = ar.events[i].core.start + ar.events[i].core.dur;
                double iEnd = im->core.start + im->core.dur;
                if (im->core.pitch == ar.events[i].core.pitch
                    && ar.events[i].core.start < iEnd - 1e-9
                    && im->core.start < aEnd - 1e-9)
                    masked = true;
            }
            if (!masked)
                events[nEvents++] = ar.events[i];
        }
    }

    // --- perc ---
    if (layers_has(params.layers, params.layerCount, ANO_MUSIC_PERC)) {
        AnoGenParams percParams = params;
        if (pos.kind == ANO_SEG_CODETTA)
            percParams.noteDensity = params.noteDensity * 0.5;
        const AnoGroove *groove = NULL;
        if (cfg->phraseGroove && pos.phrase >= 0) {
            uint32_t gr = ano_ring_phrase(pos.phrase);
            if (!ano_phrase_live(st->grooveTag, pos.phrase)) {
                AnoMusicRng r;
                eng_stream2(e, &r, "perc-pattern", pos.phrase);
                st->grooves[gr] = ano_make_groove(&r, cfg->meter,
                                                  percParams.noteDensity,
                                                  percParams.roughness);
                st->grooveTag[gr] = pos.phrase;
            }
            groove = &st->grooves[gr];
        }
        AnoMusicRng r;
        eng_stream2(e, &r, "perc", bar);
        AnoPercResult prr;
        ano_generate_perc(&ctx, cfg->meter, &percParams, pos, st->lastFill, &cfg->perc,
                          &r, groove,
                          cfg->form.hypermeter && pos.kind == ANO_SEG_REGULAR ? 0.35 : 0.0,
                          &prr);
        for (uint32_t i = 0; i < prr.eventCount; ++i)
            events[nEvents++] = prr.events[i];
        st->lastFill = prr.fill;
    }

    sort_events(events, nEvents); // canonical raw-IR order
    out->rawEventCount = nEvents;

    // per-layer modifier chains, LAYER_NAMES order
    uint32_t nFinal = 0;
    for (uint8_t layer = 0; layer < ANO_MUSIC_LAYER_COUNT; ++layer) {
        AnoMusicEvent buf[ANO_BAR_MAX_EVENTS];
        uint32_t n = 0;
        for (uint32_t i = 0; i < nEvents; ++i)
            if (events[i].core.layer == layer)
                buf[n++] = events[i];
        if (!n)
            continue;
        if (cfg->useChains) {
            AnoModifier chain[4];
            uint32_t cl = ano_default_chain(layer, cfg->performChains, chain);
            if (cl) {
                AnoMusicRng r;
                eng_stream3(e, &r, "mod", ANO_LAYER_NAMES[layer], bar);
                n = ano_apply_chain(chain, cl, buf, n, ANO_BAR_MAX_EVENTS, &ctx,
                                    cfg->meter, &params, &r);
            }
        }
        for (uint32_t i = 0; i < n; ++i)
            out->events[nFinal++] = buf[i];
    }
    sort_events(out->events, nFinal);
    out->eventCount = nFinal;

    if (st->hasModulation && bar == st->modulation.arrivalBar) {
        st->keyTonic = st->modulation.targetTonic;
        st->hasModulation = false;
        st->lastKeyPhrase = pos.phrase;
    }

    out->context = ctx;
    out->params = params;
    out->affect[0] = e->affect.valence;
    out->affect[1] = e->affect.energy;
    out->affect[2] = e->affect.tension;
    st->bar += 1;
}

/* Digest */

// Python float.hex()-exact text (13 mantissa digits; 0.0 special-cased).
static void py_hex(double x, char *buf, size_t cap)
{
    if (x == 0.0)
        snprintf(buf, cap, "%s", copysign(1.0, x) < 0 ? "-0x0.0p+0" : "0x0.0p+0");
    else
        snprintf(buf, cap, "%.13a", x);
}

// Streamed event-by-event (no staging buffer 〜 truncate would false-match).
uint64_t ano_events_digest(const AnoMusicEvent *events, uint32_t count)
{
    AnoBlake2b8 st;
    ano_blake2b8_init(&st);
    for (uint32_t i = 0; i < count; ++i) {
        char s[32], d[32], rec[160];
        py_hex(events[i].core.start, s, sizeof s);
        py_hex(events[i].core.dur, d, sizeof d);
        int n = snprintf(rec, sizeof rec, "%s,%s,%d,%d,%d,%d,%d,%s;", s, d,
                         events[i].core.pitch, events[i].core.velocity,
                         events[i].core.layer, events[i].core.tie, events[i].degree,
                         events[i].role);
        if (n < 0)
            continue;
        if ((size_t)n >= sizeof rec) // cannot happen: role is 20, hexfloats ~25
            n = (int)sizeof rec - 1;
        ano_blake2b8_update(&st, rec, (size_t)n);
    }
    uint8_t digest[8];
    ano_blake2b8_final(&st, digest);
    uint64_t v = 0;
    for (int b = 0; b < 8; ++b)
        v = v << 8 | digest[b];
    return v;
}
