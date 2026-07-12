/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_music.h
 * @brief The music IR: the authoritative event schema shared by the generation
 *        core (src/music/, TECH_SPEC port) and the synthesizer (src/synth/).
 *
 * Header-level dependency only — this header exists before the music module
 * has an implementation, because the IR is the synth's input type (TECH_SPEC
 * section 4). It carries the playable core of a NoteEvent; the inspection
 * annotations (degree, chord symbol, role) live in a dev-build sidecar that
 * arrives with the music module, never here.
 *
 * Units: `start` and `dur` are quarter-note beats from piece start. Beats map
 * to seconds only through the piecewise-constant tempo map (BeatClock,
 * TECH_SPEC section 11.1), built in full before any event is scheduled.
 *
 * It also carries the engine's game-facing contract (TECH_SPEC section 9.1):
 * the opaque AnoMusicEngine, the config that authors WHAT it plays and HOW it
 * responds, and the four control calls — affect, override, request key, request
 * motif. What it deliberately does NOT carry is the generators' own tuning
 * (how the bass picks an approach tone, what a voicing costs): that is
 * implementation, it stays in src/music/, and it is not something a game
 * authors.
 */

#ifndef ANOPTIC_MUSIC_H
#define ANOPTIC_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The six layers, canonical order, fixed (TECH_SPEC 4.1).
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

// Tie flags (TECH_SPEC 4.2). A note crossing a barline is a chain of grid- and
// bar-legal halves flagged out -> both... -> in; the chain IS one musical note
// and merge_ties (synth-side) recovers it. An orphan out dissolves into a
// plain note; an orphan in passes through struck.
typedef enum AnoMusicTie
{
    ANO_MUSIC_TIE_NONE = 0,
    ANO_MUSIC_TIE_OUT,
    ANO_MUSIC_TIE_IN,
    ANO_MUSIC_TIE_BOTH,
} AnoMusicTie;

// The playable core of one note. Articulation is applied by the generation
// side's modifier layer before events reach this schema — `dur` is the gated
// sounding duration; a voice's own release tail extends past it.
typedef struct AnoNoteEvent
{
    double  start;    // absolute beats, >= 0
    double  dur;      // beats, > 0
    uint8_t pitch;    // MIDI 0..127
    uint8_t velocity; // 1..127
    uint8_t layer;    // AnoMusicLayer
    uint8_t tie;      // AnoMusicTie
} AnoNoteEvent;

// One tempo anchor: piecewise-constant bpm from `beat` until the next anchor.
// Anchors are monotonic in beat; a point at an existing anchor's beat replaces
// its bpm. Cadence ritardandi are realized purely as extra points.
typedef struct AnoTempoPoint
{
    double beat;
    double bpm;
} AnoTempoPoint;

// The affect triple published to the control plane per bar. The synth consumes
// it for the console's affect-coupled stages (duck depth, shimmer gain and
// density, texture voices).
typedef struct AnoMusicAffect
{
    float valence; // -1 .. 1
    float energy;  //  0 .. 1
    float tension; //  0 .. 1
} AnoMusicAffect;

// The Tier-2 parameter block, produced per bar by the control plane (TECH_SPEC
// 4.4; defaults are tuning). An immutable value struct: the conductor derives
// variants by copy. The DSP-tier fields (filterCutoff .. stereoWidth) reach
// the audio library as retarget values, never per-sample automation. String
// fields of the prototype are interned: `instruments` holds an AnoPatchName per
// layer (NOT a backend's own id — the composer names a timbre and the backend
// decides what plays it), 0 = the layer's default.
typedef struct AnoMusicalParams
{
    double   tempoBpm;         // 100.0
    float    noteDensity;      // 0.5
    float    roughness;        // 0.0
    float    articulation;     // 0.9 (already baked into event durations)
    uint8_t  velocityCenter;   // 80
    uint8_t  accentDepth;      // 12
    uint8_t  registerCenter;   // 72
    uint8_t  layersActive;     // bitmask by AnoMusicLayer; bit set = sounding
    float    harmonicRhythm;   // 1.0
    float    dissonanceBudget; // 0.0
    uint16_t instruments[ANO_MUSIC_LAYER_COUNT]; // AnoPatchName; 0 = layer default

    // DSP tier
    float filterCutoff; // Hz, 2500.0
    float reverbSend;   // 0.20 (global multiplier on per-layer static sends)
    float delaySend;    // 0.10 (same)
    float drive;        // 0.15
    float stereoWidth;  // 0.70
} AnoMusicalParams;

// ---------------------------------------------------------------------------
// Theory vocabulary (the words the config and the control calls are spelled in)
// ---------------------------------------------------------------------------

typedef struct AnoMeter
{
    int numerator;
    int denominator;
} AnoMeter;

// Enum ORDER is load-bearing: the mapper walks modes along the brightness axis
// in this order, so renumbering changes the music.
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
    ANO_MODE_NONE = -1, // valence-driven (with a mapper) / ionian (without)
} AnoMode;

typedef enum AnoCadencePolicy
{
    ANO_CADENCE_AUTHENTIC = 0,
    ANO_CADENCE_HALF,
    ANO_CADENCE_DECEPTIVE,
    ANO_CADENCE_NONE = -1, // not a cadence bar
} AnoCadencePolicy;

// The texture ladder, lean -> rich. NONE is "no texture system".
typedef enum AnoTexture
{
    ANO_TEX_NONE = 0,
    ANO_TEX_MONOPHONIC,
    ANO_TEX_HOMOPHONIC,
    ANO_TEX_DOUBLED,
    ANO_TEX_IMITATIVE,
    ANO_TEX_COUNTER,
} AnoTexture;

// Semantic patch names (the mapper's energy tiers pick among these; the synth
// maps them to voice variants).
typedef enum AnoPatchName
{
    ANO_PATCH_NONE = 0,   // the layer's own default
    // pads
    ANO_PATCH_WARM,       // tight-detune 3-saw, mono, slow attack
    ANO_PATCH_BRIGHT,     // wide-detune 3-saw, stereo spread, fast attack
    ANO_PATCH_MORPH,      // morphing wavetable
    ANO_PATCH_BREEZE,     // texture (reserved by the synth; falls back)
    // basses
    ANO_PATCH_ROUND,      // saw + sub sine, filter-envelope pluck
    ANO_PATCH_DRIVEN,     // the same, tanh pre-drive, hotter sweep
    ANO_PATCH_BAD_GROUND, // texture (reserved)
    // melodic voices
    ANO_PATCH_SOFT,       // lead: tri + saw, delayed vibrato
    ANO_PATCH_HARD,       // lead: saw + square, faster hotter vibrato
    ANO_PATCH_MELLOW,     // lead: tri + sine — the countermelody voice
    ANO_PATCH_KEYS,       // repitched bell sampler
    ANO_PATCH_WHISTLE,    // texture (reserved)
    // arps
    ANO_PATCH_PLUCK,      // 2-op FM
    ANO_PATCH_GLASS,      // 2-op FM, higher ratio
    ANO_PATCH_CHIMES,     // 5-partial tubular additive
    ANO_PATCH_COUNT,
} AnoPatchName;

// Name <-> id, for config text and authored fixtures. Unknown name returns 0.
uint32_t    ano_music_patch_id(const char *name);
const char *ano_music_patch_name(uint32_t id);

// ---------------------------------------------------------------------------
// Authored motifs (TECH_SPEC section 5.5: the game's one place to bind meaning)
// ---------------------------------------------------------------------------

#define ANO_RHYTHM_MAX 32 // >= any bar's slot count
#define ANO_MOTIF_MAX  ANO_RHYTHM_MAX
#define ANO_SIG_MAX    16 // authored signature motifs

typedef struct AnoRhythmNote
{
    int slot;
    int durSlots;
} AnoRhythmNote;

typedef enum AnoContourShape
{
    ANO_SHAPE_ARCH = 0,
    ANO_SHAPE_DESCENT,
    ANO_SHAPE_ASCENT,
    ANO_SHAPE_ZIGZAG,
    ANO_SHAPE_COUNT,
} AnoContourShape;

// A cell: when each note falls in the bar, and its diatonic offset from the
// bar's anchor. Pitches are never authored — the motif is transposed and
// re-realized into whatever harmony it lands in, which is what lets it stay
// recognizable while the music moves.
typedef struct AnoMotif
{
    AnoRhythmNote rhythm[ANO_MOTIF_MAX];
    int           contour[ANO_MOTIF_MAX];
    uint32_t      n;
    uint8_t       shape; // AnoContourShape
} AnoMotif;

#define ANO_MOTIF_TAG_MAX 16

// The tag is COPIED, not borrowed: the engine must stay pointer-free (that is
// what lets a snapshot be its bytes), and a borrowed string would also dangle
// the moment a game freed it.
typedef struct AnoSignatureMotif
{
    char     tag[ANO_MOTIF_TAG_MAX]; // "hero", "threat", ... — a request's handle
    AnoMotif motif;
    double   importance; // 0..1: identity landmark (high) vs colour (low)
} AnoSignatureMotif;

// ---------------------------------------------------------------------------
// The mapping table: affect -> Tier-2 parameters (TECH_SPEC section 10)
// ---------------------------------------------------------------------------
// The score's personality, and the reason two games (or two zones) can share
// this engine and not sound alike. Pure tuning: no engine internals here.

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
    // tempo (BPM): dominated by energy, tinted by valence
    double tempoBase;        // 70.0
    double tempoEnergy;      // 80.0
    double tempoValence;     // 8.0
    double tempoRange[2];    // 60.0, 160.0
    double tempoSlewPerBeat; // 2.0

    // melody register center (MIDI)
    double registerBase;    // 72.0
    double registerValence; // 4.0
    double registerTension; // 2.0

    // note density / rhythmic roughness
    double densityBase;      // 0.15
    double densityEnergy;    // 0.75
    double roughnessBase;    // 0.10
    double roughnessEnergy;  // 0.30
    double roughnessTension; // 0.20
    double roughnessMax;     // 0.60

    // articulation gate: legato 1.05 .. staccato 0.45
    double articulationLegato;     // 1.05
    double articulationEnergyDrop; // 0.60
    double articulationSlewPerBar; // 0.15

    // dynamics
    double velocityBase;       // 56.0
    double velocityEnergy;     // 44.0
    double velocitySlewPerBar; // 10.0
    double accentBase;         // 4.0
    double accentEnergy;       // 14.0

    uint8_t      layerGateCount; // 5
    AnoLayerGate layerGates[8];
    double       layerHysteresis; // 0.10

    // mode selection (brightness axis), phrase-quantized in the conductor
    double modeHysteresis; // 0.60

    // instrument swaps by energy, phrase-quantized in the conductor
    uint8_t          instrumentRowCount; // 4
    AnoInstrumentRow instrumentRows[ANO_MUSIC_LAYER_COUNT];
    double           instrumentHysteresis; // 0.08

    // cadence policy by tension
    double cadenceAuthenticMax; // 0.35
    double cadenceHalfMax;      // 0.65

    // slow harmonic rhythm (2-bar chords) when calm
    double harmonicSlowEnergy;  // 0.30
    double harmonicSlowTension; // 0.50

    // DSP tier
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

// A second personality, and the point of the table being public: the same engine,
// the same seed, a different band. Where the default reaches for the acoustic
// vocabulary (saw pads, a plucked bass, a lead over the top), this one reaches for
// the synthetic one — a wavetable bass, filter-envelope plucks carrying the
// melody, a soft lead in the arp. Nothing here is a different ENGINE; it is a
// different set of instruments handed to the same composer.
AnoMappingTable ano_mapping_table_electronic(void);

// ---------------------------------------------------------------------------
// The dramaturg: the tension ledger (TECH_SPEC section 5.8)
// ---------------------------------------------------------------------------
// Withholds resolution while tension accrues and spends it when tension breaks.
// The other half of the score's personality: how patient it is, how big the
// payoff, whether the debt is paid in dissonance or in a lament bass.

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

// ---------------------------------------------------------------------------
// Feature waves. Every flag off == the byte-identical baseline.
// ---------------------------------------------------------------------------

typedef struct AnoFormConfig
{
    bool   cadential64;    // B1: prepared authentic cadences, I64 -> V -> I
    bool   periods;        // B2: antecedent-consequent phrase pairs
    double periodProb;     // 0.65
    bool   hypermeter;     // B3: bar weight within the phrase group
    bool   bassInversions; // B4: stepwise bass via first inversions
    bool   split64;        // D3: the 6/4 compressed into the pre-cadence bar
} AnoFormConfig;

typedef struct AnoTextureConfig
{
    bool doubling;  // C1: parallel 3rds/6ths inside the melody
    bool animate;   // C2: pad figuration instead of a static block
    bool imitation; // C3: the phrase cell echoed in the arp register
    bool rotate;    // C4: texture as a Tier-2 parameter, chosen per phrase
    bool counter;   // C5: the countermelody layer
} AnoTextureConfig;

typedef struct AnoClockConfig
{
    bool   codetta;          // D2: a tonic afterglow appended to a big spend
    bool   extension;        // the pre-dominant stretched while withholding
    bool   elision;          // the next phrase starts ON the cadence bar
    double codettaPayoff;    // 0.45
    int    codettaBars;      // 2
    double extensionTension; // 0.7
    double elisionEnergy;    // 0.75
} AnoClockConfig;

typedef struct AnoTieConfig
{
    bool anacrusis;   // D1: cadence-bar pickups into the next downbeat
    bool suspension;  // the M14 preparation genuinely HELD across the barline
    bool syncopation; // rough bars push their last note through the barline
} AnoTieConfig;

typedef struct AnoMelodyFlags
{
    bool planApex;    // A4: one planned melodic apex per phrase
    bool counterpoint; // A3: guard the melody-bass outer-voice frame
} AnoMelodyFlags;

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

// What to play, and how it answers. The generators' own tuning is NOT here by
// design: it is implementation, and it stays in src/music/ on its defaults.
typedef struct AnoMusicConfig
{
    AnoMeter meter;    // 4/4
    int      keyTonic; // pitch class 0..11
    int      mode;     // AnoMode; NONE = valence-driven (mapper) / ionian
    float    valence, energy, tension; // initial affect
    int      phraseBars;    // 8
    int      wanderPhrases; // auto-modulate every N phrases; -1 = never

    // Cadence plan: an explicit cycle, or (count 0) tension-driven with a
    // mapper / the default cycle without one.
    int8_t   cadencePolicies[8]; // AnoCadencePolicy
    uint32_t cadencePolicyCount;

    // Personality. hasMapper = false pins the params to `params` (the static
    // path); hasDramaturg = false leaves the ledger inert.
    bool               hasMapper;
    AnoMappingTable    mapper;
    bool               hasDramaturg;
    AnoDramaturgConfig dramaturg;
    AnoMusicalParams   params; // static path only (hasMapper == false)

    // Authored meaning: the motifs ano_music_request_motif can ask for.
    AnoSignatureMotif motifLibrary[ANO_SIG_MAX];
    uint32_t          motifLibraryCount;
    double            motifLeniency; // 0.5

    double cadenceRit;   // A1: fractional tempo dip into a cadence; 0 = off
    bool   phraseGroove; // A2: pin the perc/arp pattern per phrase
    AnoFormConfig    form;
    AnoTextureConfig texture;
    AnoTieConfig     ties;
    AnoClockConfig   clock;
    AnoMelodyFlags   melody;

    bool useChains;     // performance modifiers (humanize, swing, strum, ...)
    bool performChains; // ...with the expressive Perform pass
} AnoMusicConfig;

AnoMusicConfig ano_music_config_default(void);

// The generator. Opaque: its state is the port's sequential machinery and no
// caller has business reading it. Pointer-free internally, which is what makes
// snapshot/restore a memcpy.
typedef struct AnoMusicEngine AnoMusicEngine;

AnoMusicEngine *ano_music_create(const AnoMusicConfig *cfg, uint64_t seed);
void            ano_music_destroy(AnoMusicEngine *e);

// ---------------------------------------------------------------------------
// The control plane (TECH_SPEC section 9.1: affect in, nothing game-semantic)
// ---------------------------------------------------------------------------
// These are legal at any wall-clock moment; each takes effect at the musical
// boundary its parameter is quantized to (section 9.3). Game state -> affect is
// deliberately out of scope: it is a game-specific model, not the engine's.

// Merge affect axes; NAN leaves an axis alone. `urgent` demotes the
// phrase-quantized changes (mode, instruments) to the next barline.
void ano_music_set_affect(AnoMusicEngine *e, float valence, float energy,
                          float tension, bool urgent);

// Queue a pivot-chord modulation to a new tonic. It rides the next available
// phrase cadence; `urgent` starts at the earliest ungenerated bar instead.
void ano_music_request_key(AnoMusicEngine *e, int tonicPc, bool urgent);

// Ask the signature director to state an authored motif at the next musically
// sound phrase boundary. Persists until honoured; an unknown tag is a no-op.
void ano_music_request_motif(AnoMusicEngine *e, const char *tag);

// Pin one Tier-2 parameter, or release it back to the mapper. The id spellings
// are the parameter names ("tempo_bpm", "reverb_send", "texture", ...).
bool ano_music_set_override(AnoMusicEngine *e, const char *param, double value);
void ano_music_clear_override(AnoMusicEngine *e, const char *param);

// ---------------------------------------------------------------------------
// Generation and reconstruction
// ---------------------------------------------------------------------------

#define ANO_MUSIC_MAX_BAR_EVENTS 256
#define ANO_MUSIC_MAX_TEMPO      8

// What a bar MEANS, for gameplay that reacts to the music: the sounding key and
// chord, and whether this bar is a cadence, a key arrival, or a motif landing.
// It is a type of its own because it travels on its own — the synth generates
// bars ahead of the playhead, so the meaning is held back and delivered when the
// bar SOUNDS (AEVT_MUSIC_BAR), not when it was composed.
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

// One bar's worth of the piece, in the shapes the synth consumes.
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

// Generate the next bar. Microseconds; safe to call from the audio thread at a
// bar edge (the measured worst case is a small fraction of one block period).
void ano_music_advance_bar(AnoMusicEngine *e, AnoMusicBar *out);

// Bar length in quarter-note beats (the meter's: 4/4 -> 4.0, 6/8 -> 3.0). The
// synth's live schedule needs it before the first bar has been generated.
double ano_music_bar_quarters(const AnoMusicEngine *e);

// The bar ano_music_advance_bar will produce next (0 on a fresh engine).
int ano_music_next_bar(const AnoMusicEngine *e);

// Save/restore. The engine is pointer-free and its padding is deterministic, so
// a snapshot IS its bytes — two engines built from the same config and seed and
// advanced to the same bar are byte-identical. That is what makes seek work:
// rebuild off the audio thread, hand the bytes over, adopt at a bar edge. It is
// also what makes a save file possible, and a state checksum meaningful.
size_t ano_music_snapshot_size(void);
bool   ano_music_snapshot(const AnoMusicEngine *e, void *buf, size_t cap);
bool   ano_music_restore(AnoMusicEngine *e, const void *buf, size_t len);

#endif // ANOPTIC_MUSIC_H
