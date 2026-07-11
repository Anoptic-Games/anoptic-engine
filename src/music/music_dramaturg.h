/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_dramaturg.h (private to src/music/)
 * The dramaturg (musicgen/gen/dramaturg.py): a tension-debt ledger between
 * the affect stream and the memoryless mapper. Withholding rations the
 * authentic cadence (deceptive instead), escalates gate/register pressure,
 * and anchors buildups on a dominant pedal or lament ground; a spend cashes
 * the ledger as a graded payoff (mode brightening, snapped-back tiers). All
 * draw-free; the ledger is a pure function of (seed, affect trajectory,
 * bar). Trace strings are elided (inspection-only in the prototype).
 */

#ifndef ANO_MUSIC_DRAMATURG_H
#define ANO_MUSIC_DRAMATURG_H

#include "music_form.h"

typedef struct AnoLedger
{
    int    barsSinceAuthentic;
    int    deceptions;
    int    withholdingPhrases;
    double peakTension;
    double prevBaseTension;
    bool   hasPrevBaseTension;
    int    cadenceTag[ANO_PHRASE_WINDOW];    // owning phrase, ANO_SLOT_EMPTY = unset
    int8_t phraseCadence[ANO_PHRASE_WINDOW]; // AnoCadencePolicy
    bool   suppressTonic; // per-bar: the walk circles the tonic
    bool   lament;        // per-bar (B4): the walk rides the lament ground
    int    buildups;      // completed withhold->spend cycles
    double lastSpend;
} AnoLedger;

void ano_ledger_init(AnoLedger *l);

// The cadence the dramaturg forced on a phrase, ANO_CADENCE_NONE if it let the
// phrase alone (or if the phrase has aged out of the ring, which no caller can
// reach: the ledger is read inside the phrase it was written for).
static inline int8_t ano_ledger_cadence(const AnoLedger *l, int phrase)
{
    if (!ano_phrase_live(l->cadenceTag, phrase))
        return ANO_CADENCE_NONE;
    return l->phraseCadence[ano_ring_phrase(phrase)];
}

static inline void ano_ledger_set_cadence(AnoLedger *l, int phrase, int8_t policy)
{
    if (phrase < 0)
        return;
    l->cadenceTag[ano_ring_phrase(phrase)] = phrase;
    l->phraseCadence[ano_ring_phrase(phrase)] = policy;
}

// What the dramaturg asks of a bar; all-neutral when idle.
typedef struct AnoDirective
{
    int8_t   cadence; // AnoCadencePolicy; NONE = no forced policy
    uint8_t  lockLayers[2];
    uint32_t lockLayerCount;
    int      registerCap;
    int      brighten;
    double   intensify;
    bool     withholdRootTonic;
    int      escalation;
    double   payoff; // > 0 on a spend bar
    bool     suspend;      // M14: request a prepared pad suspension
    bool     appoggiatura; // M14: allow an unprepared pad lean
    int      pedal;        // M14: bass pedal degree (0 none; 5 dominant)
    bool     lament;       // B4
} AnoDirective;

// The graded payoff: strictly increasing in accrued debt, saturating in
// [0, 1).
double ano_spend_magnitude(const AnoLedger *l, const AnoDramaturgConfig *cfg);

// Observe one realized bar; update the ledger; return the bar's directive.
// The accrue/spend decision is taken once per phrase at pos 0.
AnoDirective ano_dramaturg_on_bar(const AnoDramaturgConfig *cfg, AnoLedger *l,
                                  double baseTension, AnoPhrasePos pos);

#endif // ANO_MUSIC_DRAMATURG_H
