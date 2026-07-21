/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Music IR + game-facing engine: note events, tempo map, affect, Tier-2 params, control plane.
// Units: start/dur are quarter-note beats from piece start. Beats -> seconds via the tempo map.
// Dev-build inspection fields (degree, chord symbol, role) live in src/music/, never here.

#ifndef ANOPTIC_MUSIC_H
#define ANOPTIC_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Six layers, canonical order, fixed.
typedef enum AnoMusicLayer
{
    ANO_MUSIC_PAD = 0,
    ANO_MUSIC_BASS,
    ANO_MUSIC_MELODY,
    ANO_MUSIC_COUNTER,
    ANO_MUSIC_ARP,
    ANO_MUSIC_PERC,
    ANO_MUSIC_LAYER_COUNT,
} AnoMusicLayer;

// Tie chain: out -> both... -> in is one musical note; merge_ties recovers it.
// Orphan out -> plain note; orphan in passes through struck.
typedef enum AnoMusicTie
{
    ANO_MUSIC_TIE_NONE = 0,
    ANO_MUSIC_TIE_OUT,
    ANO_MUSIC_TIE_IN,
    ANO_MUSIC_TIE_BOTH,
} AnoMusicTie;

// Playable core. dur is gated sounding duration; release tail may extend past it.
typedef struct AnoNoteEvent
{
    double  start;    // absolute beats, >= 0
    double  dur;      // beats, > 0
    uint8_t pitch;    // MIDI 0..127
    uint8_t velocity; // 1..127
    uint8_t layer;    // AnoMusicLayer
    uint8_t tie;      // AnoMusicTie
} AnoNoteEvent;

// Piecewise-constant bpm from beat until the next anchor. Monotonic in beat; same beat replaces.
// Cadence ritardandi are extra points on this map.
typedef struct AnoTempoPoint
{
    double beat;
    double bpm;
} AnoTempoPoint;

// Affect triple published per bar.
typedef struct AnoMusicAffect
{
    float valence; // -1 .. 1
    float energy;  //  0 .. 1
    float tension; //  0 .. 1
} AnoMusicAffect;

// Tier-2 params per bar. Immutable value struct; conductor derives variants by copy.
// DSP fields are retarget values, not per-sample automation.
// instruments[]: AnoPatchName per layer; 0 = layer default.
typedef struct AnoMusicalParams
{
    double   tempoBpm;         // 100.0
    float    noteDensity;      // 0.5
    float    roughness;        // 0.0
    float    articulation;     // 0.9 (baked into event durations)
    uint8_t  velocityCenter;   // 80
    uint8_t  accentDepth;      // 12
    uint8_t  registerCenter;   // 72
    uint8_t  layersActive;     // bitmask by AnoMusicLayer; bit set = sounding
    float    harmonicRhythm;   // 1.0
    float    dissonanceBudget; // 0.0
    uint16_t instruments[ANO_MUSIC_LAYER_COUNT]; // AnoPatchName; 0 = layer default

    /* DSP tier */
    float filterCutoff; // Hz, 2500.0
    float reverbSend;   // 0.20 (global multiplier on per-layer static sends)
    float delaySend;    // 0.10
    float drive;        // 0.15
    float stereoWidth;  // 0.70
} AnoMusicalParams;


/* Theory Vocabulary */

typedef struct AnoMeter
{
    int numerator;
    int denominator;
} AnoMeter;

// Enum ORDER is load-bearing: mapper walks modes along the brightness axis in this order; renumbering changes the music.
typedef enum AnoMode
{
    ANO_MODE_IONIAN = 0,
    ANO_MODE_DORIAN,
    ANO_MODE_PHRYGIAN,
    ANO_MODE_LYDIAN,
    ANO_MODE_MIXOLYDIAN,
    ANO_MODE_AEOLIAN,
    ANO_MODE_LOCRIAN,
    ANO_MODE_COUNT,
    ANO_MODE_NONE = -1, // valence-driven (with mapper) / ionian (without)
} AnoMode;

typedef enum AnoCadencePolicy
{
    ANO_CADENCE_AUTHENTIC = 0,
    ANO_CADENCE_HALF,
    ANO_CADENCE_DECEPTIVE,
    ANO_CADENCE_NONE = -1, // not a cadence bar
} AnoCadencePolicy;

// Texture ladder, lean -> rich. NONE = no texture system.
typedef enum AnoTexture
{
    ANO_TEX_NONE = 0,
    ANO_TEX_MONOPHONIC,
    ANO_TEX_HOMOPHONIC,
    ANO_TEX_DOUBLED,
    ANO_TEX_IMITATIVE,
    ANO_TEX_COUNTER,
} AnoTexture;

// Semantic patch names. Synth maps them to voice variants.
typedef enum AnoPatchName
{
    ANO_PATCH_NONE = 0,   // layer default
    /* pads */
    ANO_PATCH_WARM,       // tight-detune 3-saw, mono, slow attack
    ANO_PATCH_BRIGHT,     // wide-detune 3-saw, stereo spread, fast attack
    ANO_PATCH_MORPH,      // morphing wavetable
    ANO_PATCH_BREEZE,     // texture (reserved; falls back)
    /* basses */
    ANO_PATCH_ROUND,      // saw + sub sine, filter-envelope pluck
    ANO_PATCH_DRIVEN,     // same + tanh pre-drive, hotter sweep
    ANO_PATCH_BAD_GROUND, // texture (reserved)
    /* melodic */
    ANO_PATCH_SOFT,       // lead: tri + saw, delayed vibrato
    ANO_PATCH_HARD,       // lead: saw + square, faster hotter vibrato
    ANO_PATCH_MELLOW,     // lead: tri + sine (countermelody)
    ANO_PATCH_KEYS,       // repitched bell sampler
    ANO_PATCH_WHISTLE,    // texture (reserved)
    /* arps */
    ANO_PATCH_PLUCK,      // 2-op FM
    ANO_PATCH_GLASS,      // 2-op FM, higher ratio
    ANO_PATCH_CHIMES,     // 5-partial tubular additive
    ANO_PATCH_COUNT,
} AnoPatchName;

// Name <-> id. Unknown name returns 0.
uint32_t    ano_music_patch_id(const char *name);
const char *ano_music_patch_name(uint32_t id);


/* Authored Motifs */

#define ANO_RHYTHM_MAX 32 // >= any bar's slot count
#define ANO_MOTIF_MAX  ANO_RHYTHM_MAX
#define ANO_SIG_MAX    16 // authored signature motifs

typedef struct AnoRhythmNote
{
    int slot;
    int durSlots;
} AnoRhythmNote;

// CONTOUR_SHAPES tuple order (feeds rng.choice).
typedef enum AnoContourShape
{
    ANO_SHAPE_ARCH = 0,
    ANO_SHAPE_DESCENT,
    ANO_SHAPE_ASCENT,
    ANO_SHAPE_ZIGZAG,
    ANO_SHAPE_COUNT,
} AnoContourShape;

// Cell: slot timing + diatonic offsets from the bar anchor. Pitches never authored.
typedef struct AnoMotif
{
    AnoRhythmNote rhythm[ANO_MOTIF_MAX];
    int           contour[ANO_MOTIF_MAX];
    uint32_t      n;
    uint8_t       shape; // AnoContourShape
} AnoMotif;

#define ANO_MOTIF_TAG_MAX 16

// Tag is COPIED, not borrowed (engine is pointer-free for snapshot = bytes).
typedef struct AnoSignatureMotif
{
    char     tag[ANO_MOTIF_TAG_MAX]; // "hero", "threat", ... 〜 request handle
    AnoMotif motif;
    double   importance; // 0..1: identity landmark (high) vs colour (low)
} AnoSignatureMotif;


/* Mapping Table */

// Affect -> Tier-2. Pure tuning; no engine internals.

typedef struct AnoLayerGate
{
    uint8_t layer; // AnoMusicLayer
    double  threshold;
} AnoLayerGate;

typedef struct AnoInstrumentTier
{
    uint8_t patch; // AnoPatchName
    double  threshold;
} AnoInstrumentTier;

typedef struct AnoInstrumentRow
{
    uint8_t layer; // AnoMusicLayer
    uint8_t tierCount;
    AnoInstrumentTier tiers[3];
} AnoInstrumentRow;

typedef struct AnoMappingTable
{
    /* tempo (BPM) */
    double tempoBase;        // 70.0
    double tempoEnergy;      // 80.0
    double tempoValence;     // 8.0
    double tempoRange[2];    // 60.0, 160.0
    double tempoSlewPerBeat; // 2.0

    /* melody register center (MIDI) */
    double registerBase;    // 72.0
    double registerValence; // 4.0
    double registerTension; // 2.0

    /* density / roughness */
    double densityBase;      // 0.15
    double densityEnergy;    // 0.75
    double roughnessBase;    // 0.10
    double roughnessEnergy;  // 0.30
    double roughnessTension; // 0.20
    double roughnessMax;     // 0.60

    /* articulation gate: legato 1.05 .. staccato 0.45 */
    double articulationLegato;     // 1.05
    double articulationEnergyDrop; // 0.60
    double articulationSlewPerBar; // 0.15

    /* dynamics */
    double velocityBase;       // 56.0
    double velocityEnergy;     // 44.0
    double velocitySlewPerBar; // 10.0
    double accentBase;         // 4.0
    double accentEnergy;       // 14.0

    uint8_t      layerGateCount; // 5
    AnoLayerGate layerGates[8];
    double       layerHysteresis; // 0.10

    /* mode (brightness axis), phrase-quantized */
    double modeHysteresis; // 0.60

    /* instrument swaps by energy, phrase-quantized */
    uint8_t          instrumentRowCount; // 4
    AnoInstrumentRow instrumentRows[ANO_MUSIC_LAYER_COUNT];
    double           instrumentHysteresis; // 0.08

    /* cadence policy by tension */
    double cadenceAuthenticMax; // 0.35
    double cadenceHalfMax;      // 0.65

    /* slow harmonic rhythm (2-bar chords) when calm */
    double harmonicSlowEnergy;  // 0.30
    double harmonicSlowTension; // 0.50

    /* DSP tier */
    double cutoffBaseHz;         // 350.0
    double cutoffEnergyOctaves;  // 4.2
    double cutoffValenceOctaves; // 0.5
    double reverbSendBase;       // 0.10
    double reverbSendTension;    // 0.30
    double reverbSendStillness;  // 0.18
    double delaySendBase;        // 0.04
    double delaySendActivity;    // 0.24
    double driveBase;            // 0.05
    double driveEnergy;          // 0.45
    double widthBase;            // 0.55
    double widthValence;         // 0.25
} AnoMappingTable;

AnoMappingTable ano_mapping_table_default(void);

// Second personality: same engine/seed, different instrument rows.
AnoMappingTable ano_mapping_table_electronic(void);


/* Dramaturg */

// Tension ledger: accrues while unresolved, spends on break.

typedef struct AnoDramaturgConfig
{
    bool    enabled;          // true; false => inert
    double  leniency;         // 0.5: 0 strict .. 1 lenient
    double  accrueAbove;      // 0.55
    double  debtGain;         // 0.12
    int     escalatePhrases;  // 2
    uint8_t holdTier;         // ANO_MUSIC_ARP
    int     registerCapMax;   // 6
    int     escalationCap;    // 4
    double  bigSpend;         // 0.7
    int     maxDebt;          // 96
    bool    earnedDissonance; // true (M14)
    bool    motifLifecycle;   // true (M15)
    bool    lamentBass;       // true (B4)
} AnoDramaturgConfig;

AnoDramaturgConfig ano_dramaturg_config_default(void);


/* Feature Flags */

// Every flag off == byte-identical baseline.

typedef struct AnoFormConfig
{
    bool   cadential64;    // B1: I64 -> V -> I
    bool   periods;        // B2: antecedent-consequent pairs
    double periodProb;     // 0.65
    bool   hypermeter;     // B3: bar weight within phrase group
    bool   bassInversions; // B4: stepwise bass via first inversions
    bool   split64;        // D3: 6/4 compressed into pre-cadence bar
} AnoFormConfig;

typedef struct AnoTextureConfig
{
    bool doubling;  // C1: parallel 3rds/6ths inside melody
    bool animate;   // C2: pad figuration
    bool imitation; // C3: phrase cell echoed in arp register
    bool rotate;    // C4: texture chosen per phrase
    bool counter;   // C5: countermelody layer
} AnoTextureConfig;

typedef struct AnoClockConfig
{
    bool   codetta;          // D2: tonic afterglow after big spend
    bool   extension;        // pre-dominant stretched while withholding
    bool   elision;          // next phrase starts ON the cadence bar
    double codettaPayoff;    // 0.45
    int    codettaBars;      // 2
    double extensionTension; // 0.7
    double elisionEnergy;    // 0.75
} AnoClockConfig;

typedef struct AnoTieConfig
{
    bool anacrusis;   // D1: cadence-bar pickups into next downbeat
    bool suspension;  // M14 prep held across barline
    bool syncopation; // rough bars push last note through barline
} AnoTieConfig;

typedef struct AnoMelodyFlags
{
    bool planApex;     // A4: one planned melodic apex per phrase
    bool counterpoint; // A3: guard melody-bass outer-voice frame
} AnoMelodyFlags;


/* Engine */

typedef struct AnoMusicConfig
{
    AnoMeter meter;    // 4/4
    int      keyTonic; // pitch class 0..11
    int      mode;     // AnoMode; NONE = valence-driven (mapper) / ionian
    float    valence, energy, tension; // initial affect
    int      phraseBars;    // 8
    int      wanderPhrases; // auto-modulate every N phrases; -1 = never

    // Cadence plan: explicit cycle, or (count 0) tension-driven / default cycle.
    int8_t   cadencePolicies[8]; // AnoCadencePolicy
    uint32_t cadencePolicyCount;

    // hasMapper false pins params; hasDramaturg false leaves ledger inert.
    bool               hasMapper;
    AnoMappingTable    mapper;
    bool               hasDramaturg;
    AnoDramaturgConfig dramaturg;
    AnoMusicalParams   params; // static path only (hasMapper == false)

    AnoSignatureMotif motifLibrary[ANO_SIG_MAX];
    uint32_t          motifLibraryCount;
    double            motifLeniency; // 0.5

    double cadenceRit;   // A1: fractional tempo dip into cadence; 0 = off
    bool   phraseGroove; // A2: pin perc/arp pattern per phrase
    AnoFormConfig    form;
    AnoTextureConfig texture;
    AnoTieConfig     ties;
    AnoClockConfig   clock;
    AnoMelodyFlags   melody;

    bool useChains;     // performance modifiers
    bool performChains; // ...with expressive Perform pass
} AnoMusicConfig;

AnoMusicConfig ano_music_config_default(void);

// Opaque generator. Pointer-free: snapshot/restore is memcpy.
typedef struct AnoMusicEngine AnoMusicEngine;

AnoMusicEngine *ano_music_create(const AnoMusicConfig *cfg, uint64_t seed);
void            ano_music_destroy(AnoMusicEngine *e);


/* Control Plane */

// Legal any wall-clock moment; takes effect at the parameter's musical boundary.
// NAN leaves an axis alone. urgent demotes phrase-quantized changes to next barline.
void ano_music_set_affect(AnoMusicEngine *e, float valence, float energy,
                          float tension, bool urgent);

// Queue pivot-chord modulation. Rides next phrase cadence; urgent = earliest ungenerated bar.
void ano_music_request_key(AnoMusicEngine *e, int tonicPc, bool urgent);

// Request authored motif at next sound phrase boundary. Persists until honoured; unknown tag no-op.
void ano_music_request_motif(AnoMusicEngine *e, const char *tag);

// Pin / release one Tier-2 param. Ids: "tempo_bpm", "reverb_send", "texture", ...
bool ano_music_set_override(AnoMusicEngine *e, const char *param, double value);
void ano_music_clear_override(AnoMusicEngine *e, const char *param);


/* Generation */

#define ANO_MUSIC_MAX_BAR_EVENTS 256
#define ANO_MUSIC_MAX_TEMPO      8

// Bar meaning for gameplay. Delivered when the bar sounds, not when composed.
typedef struct AnoMusicMeaning
{
    int    bar;
    int    keyTonic;
    int    mode;           // AnoMode
    int    chordDegree;    // 1..7
    int    chordInversion;
    int8_t cadencePolicy;  // AnoCadencePolicy; NONE = not a cadence bar
    bool   isCadence;
    bool   keyArrived;
    bool   motifStated;
} AnoMusicMeaning;

// One bar for the synth.
typedef struct AnoMusicBar
{
    AnoNoteEvent     events[ANO_MUSIC_MAX_BAR_EVENTS];
    uint32_t         eventCount;
    AnoMusicalParams params;
    AnoMusicAffect   affect;
    AnoTempoPoint    tempo[ANO_MUSIC_MAX_TEMPO];
    uint32_t         tempoCount;
    AnoMusicMeaning  meaning;
} AnoMusicBar;

// Next bar. Safe on the audio thread at a bar edge.
void ano_music_advance_bar(AnoMusicEngine *e, AnoMusicBar *out);

// Bar length in quarter-note beats (4/4 -> 4.0, 6/8 -> 3.0).
double ano_music_bar_quarters(const AnoMusicEngine *e);

// Bar index advance_bar will produce next (0 on a fresh engine).
int ano_music_next_bar(const AnoMusicEngine *e);

// Snapshot = engine bytes (pointer-free; padding deterministic). Same config+seed+bar => byte-identical.
size_t ano_music_snapshot_size(void);
bool   ano_music_snapshot(const AnoMusicEngine *e, void *buf, size_t cap);
bool   ano_music_restore(AnoMusicEngine *e, const void *buf, size_t len);

#endif // ANOPTIC_MUSIC_H
