/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Public face over the conductor. AnoMusicConfig = authored content; generator tuning stays default.

#include <anoptic_memory.h>
#include <anoptic_music.h>

#include <mimalloc.h>

#include <math.h>
#include <string.h>

#include "music_conductor.h"

AnoMusicConfig ano_music_config_default(void)
{
    AnoEngineConfig e = ano_engine_config_default();
    AnoMusicConfig c = {
        .meter = e.meter,
        .keyTonic = e.keyTonic,
        .mode = e.mode,
        .valence = (float)e.valence,
        .energy = (float)e.energy,
        .tension = (float)e.tension,
        .phraseBars = e.phraseBars,
        .wanderPhrases = e.wanderPhrases,
        .cadencePolicyCount = e.cadencePolicyCount,
        .hasMapper = e.hasMapper,
        .mapper = e.mapper,
        .hasDramaturg = e.hasDramaturg,
        .dramaturg = e.dramaturg,
        .params = ano_gen_params_bridge(&e.params),
        .motifLibraryCount = e.motifLibraryCount,
        .motifLeniency = e.motifLeniency,
        .cadenceRit = e.cadenceRit,
        .phraseGroove = e.phraseGroove,
        .form = e.form,
        .texture = e.texture,
        .ties = e.ties,
        .clock = e.clock,
        .melody = { e.melody.planApex, e.melody.counterpoint },
        .useChains = e.useChains,
        .performChains = e.performChains,
    };
    memcpy(c.cadencePolicies, e.cadencePolicies, sizeof c.cadencePolicies);
    return c;
}

// Public config -> conductor config. Generator tuning stays on defaults.
static void expand(const AnoMusicConfig *c, AnoEngineConfig *e)
{
    *e = ano_engine_config_default();
    e->meter = c->meter;
    e->keyTonic = c->keyTonic;
    e->mode = c->mode;
    e->valence = c->valence;
    e->energy = c->energy;
    e->tension = c->tension;
    if (c->phraseBars > 0)
        e->phraseBars = c->phraseBars;
    e->wanderPhrases = c->wanderPhrases;
    memcpy(e->cadencePolicies, c->cadencePolicies, sizeof e->cadencePolicies);
    e->cadencePolicyCount = c->cadencePolicyCount;
    e->hasMapper = c->hasMapper;
    e->mapper = c->mapper;
    e->hasDramaturg = c->hasDramaturg;
    e->dramaturg = c->dramaturg;

    // the static path's params arrive in the public (bridge) shape; the ORDERED
    // layer list the generators iterate is recovered from the bitmask in
    // canonical order, which is the order the gate emits anyway
    if (!c->hasMapper) {
        const AnoMusicalParams *p = &c->params;
        e->params.tempoBpm = p->tempoBpm;
        e->params.noteDensity = p->noteDensity;
        e->params.roughness = p->roughness;
        e->params.articulation = p->articulation;
        e->params.velocityCenter = p->velocityCenter;
        e->params.accentDepth = p->accentDepth;
        e->params.registerCenter = p->registerCenter;
        e->params.harmonicRhythm = p->harmonicRhythm;
        e->params.dissonanceBudget = p->dissonanceBudget;
        e->params.filterCutoff = p->filterCutoff;
        e->params.reverbSend = p->reverbSend;
        e->params.delaySend = p->delaySend;
        e->params.drive = p->drive;
        e->params.stereoWidth = p->stereoWidth;
        e->params.layerCount = 0;
        for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l)
            if (p->layersActive & (1u << l))
                e->params.layers[e->params.layerCount++] = (uint8_t)l;
        for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l)
            e->params.instruments[l] = (uint8_t)p->instruments[l];
    }

    for (uint32_t i = 0; i < c->motifLibraryCount && i < ANO_SIG_MAX; ++i)
        e->motifLibrary[i] = c->motifLibrary[i];
    e->motifLibraryCount = c->motifLibraryCount < ANO_SIG_MAX ? c->motifLibraryCount
                                                              : ANO_SIG_MAX;
    e->motifLeniency = c->motifLeniency;
    e->cadenceRit = c->cadenceRit;
    e->phraseGroove = c->phraseGroove;
    e->form = c->form;
    e->texture = c->texture;
    e->ties = c->ties;
    e->clock = c->clock;
    e->melody.planApex = c->melody.planApex;
    e->melody.counterpoint = c->melody.counterpoint;
    e->useChains = c->useChains;
    e->performChains = c->performChains;
}

// One allocation; engine stays pointer-free (snapshot = bytes).
AnoMusicEngine *ano_music_create(const AnoMusicConfig *cfg, uint64_t seed)
{
    AnoMusicEngine *e = mi_malloc(sizeof *e);
    if (!e)
        return NULL;
    AnoMusicConfig def;
    if (!cfg) {
        def = ano_music_config_default();
        cfg = &def;
    }
    AnoEngineConfig full;
    expand(cfg, &full);
    ano_engine_init(e, seed, &full);
    return e;
}

void ano_music_destroy(AnoMusicEngine *e)
{
    mi_free(e);
}

/* Control */

void ano_music_set_affect(AnoMusicEngine *e, float valence, float energy,
                          float tension, bool urgent)
{
    ano_engine_set_affect(e, (double)valence, (double)energy, (double)tension, urgent);
}

void ano_music_request_key(AnoMusicEngine *e, int tonicPc, bool urgent)
{
    ano_engine_request_key(e, tonicPc, urgent);
}

void ano_music_request_motif(AnoMusicEngine *e, const char *tag)
{
    ano_engine_request_motif(e, tag);
}

// Pinnable Tier-2 names. Unknown name refused.
typedef enum OverrideId
{
    OV_TEMPO, OV_VELOCITY, OV_ARTICULATION, OV_DENSITY, OV_ROUGHNESS,
    OV_ACCENT, OV_REGISTER, OV_HARMONIC_RHYTHM, OV_CADENCE, OV_MODE,
    OV_TEXTURE, OV_CUTOFF, OV_REVERB, OV_DELAY, OV_DRIVE, OV_WIDTH,
    OV_COUNT,
} OverrideId;

static const char *const OV_NAMES[OV_COUNT] = {
    "tempo_bpm", "velocity_center", "articulation", "note_density", "roughness",
    "accent_depth", "register_center", "harmonic_rhythm", "cadence_policy", "mode",
    "texture", "filter_cutoff", "reverb_send", "delay_send", "drive", "stereo_width",
};

static int override_id(const char *param)
{
    for (int i = 0; i < OV_COUNT; ++i)
        if (strcmp(param, OV_NAMES[i]) == 0)
            return i;
    return -1;
}

static void override_apply(AnoOverrides *o, int id, bool set, double v)
{
    switch (id) {
    case OV_TEMPO:        o->hasTempoBpm = set;       o->tempoBpm = v; break;
    case OV_VELOCITY:     o->hasVelocityCenter = set; o->velocityCenter = v; break;
    case OV_ARTICULATION: o->hasArticulation = set;   o->articulation = v; break;
    case OV_DENSITY:      o->hasNoteDensity = set;    o->noteDensity = v; break;
    case OV_ROUGHNESS:    o->hasRoughness = set;      o->roughness = v; break;
    case OV_ACCENT:       o->hasAccentDepth = set;    o->accentDepth = (int)v; break;
    case OV_REGISTER:     o->hasRegisterCenter = set; o->registerCenter = (int)v; break;
    case OV_HARMONIC_RHYTHM:
        o->hasHarmonicRhythm = set;
        o->harmonicRhythm = v;
        break;
    case OV_CADENCE: o->hasCadencePolicy = set; o->cadencePolicy = (int8_t)v; break;
    case OV_MODE:    o->hasMode = set;          o->mode = (int)v; break;
    case OV_TEXTURE: o->hasTexture = set;       o->texture = (AnoTexture)(int)v; break;
    case OV_CUTOFF:  o->hasFilterCutoff = set;  o->filterCutoff = v; break;
    case OV_REVERB:  o->hasReverbSend = set;    o->reverbSend = v; break;
    case OV_DELAY:   o->hasDelaySend = set;     o->delaySend = v; break;
    case OV_DRIVE:   o->hasDrive = set;         o->drive = v; break;
    case OV_WIDTH:   o->hasStereoWidth = set;   o->stereoWidth = v; break;
    default: break;
    }
}

bool ano_music_set_override(AnoMusicEngine *e, const char *param, double value)
{
    int id = override_id(param);
    if (id < 0)
        return false;
    override_apply(&e->overrides, id, true, value);
    return true;
}

void ano_music_clear_override(AnoMusicEngine *e, const char *param)
{
    int id = override_id(param);
    if (id >= 0)
        override_apply(&e->overrides, id, false, 0.0);
}

/* Generation */

void ano_music_advance_bar(AnoMusicEngine *e, AnoMusicBar *out)
{
    static _Thread_local AnoBarResult r; // 33 KB: too fat for the audio stack
    int keyBefore = e->scale.tonic;
    ano_engine_advance_bar(e, &r);

    out->eventCount = r.eventCount < ANO_MUSIC_MAX_BAR_EVENTS
                          ? r.eventCount
                          : ANO_MUSIC_MAX_BAR_EVENTS;
    for (uint32_t i = 0; i < out->eventCount; ++i)
        out->events[i] = r.events[i].core;
    out->params = ano_gen_params_bridge(&r.params);
    out->affect = ano_affect_bridge(r.affect);
    out->tempoCount = r.tempoPointCount < ANO_MUSIC_MAX_TEMPO ? r.tempoPointCount
                                                              : ANO_MUSIC_MAX_TEMPO;
    for (uint32_t i = 0; i < out->tempoCount; ++i)
        out->tempo[i] = (AnoTempoPoint){ r.tempoPoints[i].beat, r.tempoPoints[i].bpm };

    // what the bar MEANS: the payload gameplay reacts to
    AnoMusicMeaning *m = &out->meaning;
    m->bar = r.bar;
    m->keyTonic = e->scale.tonic;
    m->mode = e->scale.mode;
    m->chordDegree = r.context.chord.valid ? r.context.chord.degree : 0;
    m->chordInversion = r.context.chord.inversion;
    m->cadencePolicy = r.context.cadenceSlot == ANO_CTX_SLOT_NONE
                           ? ANO_CADENCE_NONE
                           : r.context.cadencePolicy;
    m->isCadence = r.context.cadenceSlot == ANO_CTX_SLOT_CADENCE;
    m->keyArrived = e->scale.tonic != keyBefore; // the modulation landed here
    m->motifStated = false;
    for (uint32_t i = 0; i < r.eventCount && !m->motifStated; ++i)
        m->motifStated = strcmp(r.events[i].role, "motif") == 0;
}

double ano_music_bar_quarters(const AnoMusicEngine *e)
{
    return ano_meter_bar_quarters(e->config.meter);
}

int ano_music_next_bar(const AnoMusicEngine *e)
{
    return e->st.bar;
}

/* Snapshot / restore */

size_t ano_music_snapshot_size(void)
{
    return sizeof(AnoMusicEngine);
}

bool ano_music_snapshot(const AnoMusicEngine *e, void *buf, size_t cap)
{
    if (!e || !buf || cap < sizeof *e)
        return false;
    memcpy(buf, e, sizeof *e); // pointer-free by construction
    return true;
}

bool ano_music_restore(AnoMusicEngine *e, const void *buf, size_t len)
{
    if (!e || !buf || len != sizeof *e)
        return false;
    memcpy(e, buf, sizeof *e);
    return true;
}
