/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Theory linter over generated IR. rule + bar are the checkable contract; message is human text.
// Standalone contracts (groove/outer/periods/texture/imitation) need params or phrase caches.

#ifndef ANO_MUSIC_VERIFY_H
#define ANO_MUSIC_VERIFY_H

#include "music_form.h"
#include "music_ir.h"
#include "music_motif.h"


/* Limits and Report */

typedef struct AnoLintLimits
{
    int    maxVoiceMove;          // 7
    int    padLo, padHi;          // 52, 79
    int    bassLo, bassHi;        // 26, 55
    int    melodyLo, melodyHi;    // 54, 90
    double melodyStrongChordRatio; // 0.8
    double leapResolutionRatio;    // 0.9
    int    leapSemitones;          // 5
    int    counterLo, counterHi;   // 55, 79
    double counterConsonanceRatio; // 0.7
    double counterOverlapRatio;    // 0.4
    const uint8_t *drumPitches;    // defaults to ANO_DRUM_PITCHES
    uint32_t       drumPitchCount;
} AnoLintLimits;

AnoLintLimits ano_lint_limits_default(void);

// PRE: grid-aligned generator output. POST: after modifier chains (slot/voicing rules off).
typedef enum AnoLintStage
{
    ANO_LINT_PRE = 0,
    ANO_LINT_POST,
} AnoLintStage;

#define ANO_LINT_MAX_VIOLATIONS 256

typedef struct AnoViolation
{
    const char *rule; // "scale", "pad-range", "tie", ...
    int         bar;  // 0-based; -1 for piece-level
    char        message[192];
} AnoViolation;

typedef struct AnoLintReport
{
    AnoViolation v[ANO_LINT_MAX_VIOLATIONS];
    uint32_t     count;
    uint32_t     dropped;
} AnoLintReport;

void ano_lint_report_reset(AnoLintReport *r);

const char *ano_violation_str(const AnoViolation *v, char *buf, uint32_t cap);


/* Rules */

// contexts keyed by ctx->bar; params[i] parallels contexts[i]. Uncovered bar: echo tail only.
// horizon: contexts at/beyond are lookahead only (negative = judge all).
// Layer buffers cap at 1024; overage reports "capacity".
void ano_lint(const AnoMusicEvent *events, uint32_t n,
              const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
              AnoMeter meter, AnoLintStage stage,
              const AnoLintLimits *limits, AnoLintReport *out);

// A2: phrase pattern identity for perc/arp under stable shaping params. Pre-modifier IR.
void ano_lint_groove(const AnoMusicEvent *events, uint32_t n,
                     const AnoHarmonicContext *contexts,
                     const AnoGenParams *params, uint32_t nctx, int horizon,
                     AnoMeter meter, AnoLintReport *out);

// A3: outer-voice frame. contraryMin default 0.5.
void ano_lint_outer(const AnoMusicEvent *events, uint32_t n,
                    const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                    AnoMeter meter, double contraryMin, AnoLintReport *out);

// B2: consequent answers antecedent rhythm unless landmark payoff overrode.
void ano_lint_periods(const AnoMusicEvent *events, uint32_t n,
                      const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                      AnoMeter meter, AnoLintReport *out);

// C4: Tier-2 texture state matches sounding events. Dormant when no texture.
void ano_lint_texture(const AnoMusicEvent *events, uint32_t n,
                      const AnoHarmonicContext *contexts,
                      const AnoGenParams *params, uint32_t nctx, int horizon,
                      AnoMeter meter, AnoLintReport *out);

// C3: imitation entry recognizability >= threshold (default 0.9). cellTag/cells = engine cache.
void ano_lint_imitation(const AnoMusicEvent *events, uint32_t n,
                        const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                        const int *cellTag, const AnoMotif *cells,
                        AnoMeter meter, double threshold, AnoLintReport *out);

#endif // ANO_MUSIC_VERIFY_H
