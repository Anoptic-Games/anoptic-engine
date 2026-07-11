/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_conductor.h (private to src/music/)
 * MusicEngine: the pull-based bar generator (musicgen/gen/conductor.py).
 * Chords generate one bar ahead so generators see next_chord; all sequential
 * state lives in AnoConductorState; every draw comes from a fresh tagged
 * stream, so parity reduces to condition gating and state-mutation ORDER —
 * both transcribed faithfully. Trace strings are elided throughout.
 */

#ifndef ANO_MUSIC_CONDUCTOR_H
#define ANO_MUSIC_CONDUCTOR_H

#include "music_arp.h"
#include "music_bass.h"
#include "music_control.h"
#include "music_counter.h"
#include "music_dramaturg.h"
#include "music_form.h"
#include "music_melody.h"
#include "music_perc.h"
#include "music_signatures.h"

#define ANO_BAR_MAX_EVENTS 256

typedef struct AnoFormConfig
{
    bool   cadential64;    // B1
    bool   periods;        // B2
    double periodProb;     // 0.65
    bool   hypermeter;     // B3
    bool   bassInversions; // B4
    bool   split64;        // D3 (needs cadential64)
} AnoFormConfig;

typedef struct AnoTextureConfig
{
    bool doubling;  // C1
    bool animate;   // C2
    bool imitation; // C3
    bool rotate;    // C4
    bool counter;   // C5
} AnoTextureConfig;

typedef struct AnoClockConfig
{
    bool   codetta;          // D2
    bool   extension;
    bool   elision;
    double codettaPayoff;    // 0.45
    int    codettaBars;      // 2
    double extensionTension; // 0.7
    double elisionEnergy;    // 0.75
} AnoClockConfig;

typedef struct AnoTieConfig
{
    bool anacrusis;   // D1
    bool suspension;
    bool syncopation;
} AnoTieConfig;

typedef struct AnoEngineConfig
{
    AnoMeter     meter;
    AnoGenParams params; // static path only
    int          keyTonic;
    int          mode; // AnoMode; ANO_MODE_NONE = valence-driven / ionian
    double       valence, energy, tension;
    int          phraseBars;
    int          wanderPhrases; // -1 = never
    int8_t       cadencePolicies[8];
    uint32_t     cadencePolicyCount; // 0 = tension-driven / default cycle
    bool               hasDramaturg;
    AnoDramaturgConfig dramaturg;
    AnoSignatureMotif motifLibrary[ANO_SIG_MAX];
    uint32_t          motifLibraryCount;
    double            motifLeniency; // 0.5
    double cadenceRit;   // A1: 0 = off
    bool   phraseGroove; // A2
    AnoFormConfig    form;
    AnoTextureConfig texture;
    AnoTieConfig     ties;
    AnoClockConfig   clock;
    bool            hasMapper;
    AnoMappingTable mapper;
    bool useChains;     // false = {} (modifiers disabled)
    bool performChains; // default_chains(perform=True)
    AnoHarmonyConfig  harmony;
    AnoVoicingConfig  voicing;
    AnoBassConfig     bass;
    AnoMelodyConfig   melody;
    AnoCounterConfig  counter;
    AnoArpConfig      arp;
    AnoPercConfig     perc;
} AnoEngineConfig;

AnoEngineConfig ano_engine_config_default(void);

// Pinned Tier-2 parameters (set_override); a has-flag per pinnable field.
typedef struct AnoOverrides
{
    bool hasTempoBpm;       double tempoBpm;
    bool hasVelocityCenter; double velocityCenter;
    bool hasArticulation;   double articulation;
    bool hasNoteDensity;    double noteDensity;
    bool hasRoughness;      double roughness;
    bool hasAccentDepth;    int accentDepth;
    bool hasRegisterCenter; int registerCenter;
    bool hasLayers;         uint8_t layers[ANO_MUSIC_LAYER_COUNT]; uint32_t layerCount;
    bool hasHarmonicRhythm; double harmonicRhythm;
    bool hasCadencePolicy;  int8_t cadencePolicy;
    bool hasMode;           int mode;
    bool hasTexture;        AnoTexture texture;
    bool hasInstruments;    uint8_t instruments[ANO_MUSIC_LAYER_COUNT];
    bool hasFilterCutoff;   double filterCutoff;
    bool hasReverbSend;     double reverbSend;
    bool hasDelaySend;      double delaySend;
    bool hasDrive;          double drive;
    bool hasStereoWidth;    double stereoWidth;
} AnoOverrides;

typedef struct AnoModulationPlan
{
    int  targetTonic;
    int  mode; // held for the window
    int  pivotBar, dominantBar, arrivalBar;
    bool hasPivot;
    AnoPivot pivot;
    bool hasCadencePhrase;
    int  cadencePhrase;
} AnoModulationPlan;

typedef struct AnoConductorState
{
    int      bar;
    AnoChord prevChord; // .valid = presence
    struct { int bar; AnoChord chord; } chordQueue[2];
    uint32_t chordQueueLen;
    int      prevVoicing[6];
    uint32_t prevVoicingLen;
    int      prevBassRoot; // ANO_NEAR_NONE
    AnoMelodyState  melody;
    AnoCounterState counter;
    int      padTie; // ANO_NEAR_NONE
    // Phrase- and bar-indexed caches: direct-mapped rings tagged with the index
    // that owns each slot (music_form.h). The index stays absolute; only the
    // storage wraps, so the piece runs indefinitely without the form machinery
    // ageing out from under it.
    int      motifTag[ANO_PHRASE_WINDOW];
    AnoMotif motifs[ANO_PHRASE_WINDOW];
    int       grooveTag[ANO_PHRASE_WINDOW];
    AnoGroove grooves[ANO_PHRASE_WINDOW];
    int      arpSkipTag[ANO_PHRASE_WINDOW];
    uint32_t arpSkips[ANO_PHRASE_WINDOW];
    int         apexTag[ANO_PHRASE_WINDOW];
    AnoApexPlan apexes[ANO_PHRASE_WINDOW];
    AnoPeriodPlanner planner;
    AnoPhraseClock   clock;
    int      elisionTag[ANO_BAR_WINDOW];
    int      elisions[ANO_BAR_WINDOW]; // resolving phrase, -1 = none
    AnoPlacedNote cadenceTail[3];
    uint32_t      cadenceTailLen;
    int      imitationTag[ANO_PHRASE_WINDOW];
    AnoMotif imitationCells[ANO_PHRASE_WINDOW];
    int     textureTag[ANO_PHRASE_WINDOW];
    uint8_t phraseTextures[ANO_PHRASE_WINDOW]; // AnoTexture; NONE = uncommitted
    int     inversionRun;
    int     lamentTag[ANO_BAR_WINDOW];
    bool    lamentBars[ANO_BAR_WINDOW];
    int      splitTag[ANO_BAR_WINDOW];
    AnoChord splits[ANO_BAR_WINDOW];
    int    splitPhraseTag[ANO_PHRASE_WINDOW];
    bool   splitPhrases[ANO_PHRASE_WINDOW];
    bool             hasLifecycle;
    AnoMotifLifecycle lifecycle;
    AnoMotifDirector director; // libraryCount 0 = absent
    bool     hasPendingSignature;
    AnoMotif pendingSignature;
    AnoMelodyStage pendingLifecycle; // COMPLETED (landmark) | STATED
    char requestedMotif[16];
    AnoLedger ledger;
    bool lastFill;
    int  keyTonic;
    bool hasPendingKey;
    int  pendingKeyPc;
    bool pendingKeyUrgent;
    bool hasModulation;
    AnoModulationPlan modulation;
    int lastKeyPhrase;
    int    currentMode; // AnoMode
    double currentTempo, currentVelocity, currentArticulation;
    uint8_t  activeLayers[ANO_MUSIC_LAYER_COUNT];
    uint32_t activeLayerCount;
    uint8_t currentInstruments[ANO_MUSIC_LAYER_COUNT]; // AnoPatchName
    int    policyTag[ANO_PHRASE_WINDOW];
    int8_t phrasePolicies[ANO_PHRASE_WINDOW]; // ANO_CADENCE_NONE = unsampled
    bool   hasLastEmittedTempo;
    double lastEmittedTempo;
    bool   hasTempoRestore;
    double tempoRestore;
} AnoConductorState;

typedef struct AnoBarResult
{
    int           bar;
    AnoMusicEvent events[ANO_BAR_MAX_EVENTS]; // post-modifier (what plays)
    uint32_t      eventCount;
    AnoMusicEvent rawEvents[ANO_BAR_MAX_EVENTS]; // pre-modifier IR
    uint32_t      rawEventCount;
    AnoHarmonicContext context;
    AnoGenParams       params;
    double             affect[3];
    struct { double beat, bpm; } tempoPoints[8];
    uint32_t tempoPointCount;
} AnoBarResult;

typedef struct AnoMusicEngine
{
    AnoEngineConfig   config;
    AnoConductorState st;
    uint64_t     seed;
    AnoAffect    affect;
    AnoOverrides overrides;
    bool         urgent;
    AnoScale     scale;
} AnoMusicEngine;

void ano_engine_init(AnoMusicEngine *e, uint64_t seed, const AnoEngineConfig *cfg);

// Partial updates via NAN (the prototype's None).
void ano_engine_set_affect(AnoMusicEngine *e, double valence, double energy,
                           double tension, bool urgent);
void ano_engine_request_key(AnoMusicEngine *e, int pc, bool urgent);
void ano_engine_request_motif(AnoMusicEngine *e, const char *tag);

void ano_engine_advance_bar(AnoMusicEngine *e, AnoBarResult *out);

// The section-8.4 comparator seam: the canonical event-stream digest — each
// event serialized as Python-float.hex()-exact text, BLAKE2b-8, big-endian
// u64. The Python golden generator emits the identical digest.
uint64_t ano_events_digest(const AnoMusicEvent *events, uint32_t count);

#endif // ANO_MUSIC_CONDUCTOR_H
