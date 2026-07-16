/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Tension-debt ledger: withhold authentic cadence, escalate, spend as graded payoff.
// Draw-free. Ledger is a pure function of (seed, affect trajectory, bar).

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
    bool   suppressTonic;
    bool   lament;        // B4
    int    buildups;
    double lastSpend;
} AnoLedger;

void ano_ledger_init(AnoLedger *l);

// ANO_CADENCE_NONE if unset, or if phrase aged out of the ring (callers only read the live phrase).
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
    bool     suspend;      // M14
    bool     appoggiatura; // M14
    int      pedal;        // M14: bass pedal degree (0 none; 5 dominant)
    bool     lament;       // B4
} AnoDirective;

double ano_spend_magnitude(const AnoLedger *l, const AnoDramaturgConfig *cfg); // [0, 1)

// Accrue/spend decided once per phrase at pos 0.
AnoDirective ano_dramaturg_on_bar(const AnoDramaturgConfig *cfg, AnoLedger *l,
                                  double baseTension, AnoPhrasePos pos);

#endif // ANO_MUSIC_DRAMATURG_H
