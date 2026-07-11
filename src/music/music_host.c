/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_host.c
 * The engine's public face (anoptic_music.h): the opaque handle, the curated
 * config, the four control calls, and the bar pull. Everything here is a thin
 * shell over the conductor — it owns no music logic, it just decides what the
 * outside world is allowed to say and to see.
 *
 * The config split is the point. AnoMusicConfig carries what a game AUTHORS —
 * what to play, how it answers affect, which motifs mean something — and the
 * conductor's own tuning (approach-tone weights, voicing costs) stays on its
 * defaults, because that is implementation and not content.
 */

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

// The public config expanded into the conductor's. Tier-3 (the generators' own
// tuning) is taken from the engine defaults and never surfaces.
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

// One allocation, and no heap handle stored inside it: the engine must stay
// POINTER-FREE, which is the whole reason a snapshot can be its bytes.
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

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

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

// The pinnable Tier-2 parameters, by the name the protocol spells them with.
// A name that is not here is refused rather than silently ignored — a typo in a
// game's override string would otherwise be a parameter that never took effect.
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

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

void ano_music_advance_bar(AnoMusicEngine *e, AnoMusicBar *out)
{
    static _Thread_local AnoBarResult r; // 33 KB: too fat for the audio stack
    int keyBefore = e->scale.tonic;
    ano_engine_advance_bar(e, &r);

    out->bar = r.bar;
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
    out->keyTonic = e->scale.tonic;
    out->mode = e->scale.mode;
    out->chordDegree = r.context.chord.valid ? r.context.chord.degree : 0;
    out->chordInversion = r.context.chord.inversion;
    out->cadencePolicy = r.context.cadenceSlot == ANO_CTX_SLOT_NONE
                             ? ANO_CADENCE_NONE
                             : r.context.cadencePolicy;
    out->isCadence = r.context.cadenceSlot == ANO_CTX_SLOT_CADENCE;
    out->keyArrived = e->scale.tonic != keyBefore; // the modulation landed here
    out->motifStated = false;
    for (uint32_t i = 0; i < r.eventCount && !out->motifStated; ++i)
        out->motifStated = strcmp(r.events[i].role, "motif") == 0;
}

// ---------------------------------------------------------------------------
// Snapshot / restore (the mechanism seek and save are built from)
// ---------------------------------------------------------------------------

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
