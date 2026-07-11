/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_dramaturg.c
 * Ledger arithmetic. Parity notes: intensify is Python TRUE division
 * (rung / escalation_cap as doubles); the release level keeps the
 * prototype's op order; phrase cadences persist across the whole piece
 * (the dict never evicts).
 */

#include "music_dramaturg.h"

AnoDramaturgConfig ano_dramaturg_config_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoDramaturgConfig k = {
        .enabled = true, .leniency = 0.5, .accrueAbove = 0.55, .debtGain = 0.12,
        .escalatePhrases = 2, .holdTier = ANO_MUSIC_ARP, .registerCapMax = 6,
        .escalationCap = 4, .bigSpend = 0.7, .maxDebt = 96,
        .earnedDissonance = true, .motifLifecycle = true, .lamentBass = true,
    };
    return k;
}

void ano_ledger_init(AnoLedger *l)
{
    *l = (AnoLedger){ 0 };
    for (uint32_t i = 0; i < ANO_PHRASE_WINDOW; ++i) {
        l->cadenceTag[i] = ANO_SLOT_EMPTY;
        l->phraseCadence[i] = ANO_CADENCE_NONE;
    }
}

double ano_spend_magnitude(const AnoLedger *l, const AnoDramaturgConfig *cfg)
{
    int debt = l->barsSinceAuthentic + 2 * l->deceptions;
    if (debt > cfg->maxDebt)
        debt = cfg->maxDebt;
    return 1.0 - 1.0 / (1.0 + cfg->debtGain * debt);
}

static double release_level(const AnoDramaturgConfig *cfg)
{
    return cfg->accrueAbove * (0.4 + 0.6 * cfg->leniency);
}

static int ledger_debt(const AnoLedger *l)
{
    return l->barsSinceAuthentic + l->deceptions;
}

// (register_cap, lock, intensify, rung) for the current withholding rung.
static void withholding(const AnoDramaturgConfig *cfg, const AnoLedger *l,
                        AnoDirective *d)
{
    int div = cfg->escalatePhrases > 1 ? cfg->escalatePhrases : 1;
    int rung = l->withholdingPhrases / div;
    d->escalation = rung;
    if (rung < 1)
        return;
    int cap = rung * 2;
    if (cap > cfg->registerCapMax)
        cap = cfg->registerCapMax;
    d->registerCap = cap;
    d->lockLayers[0] = cfg->holdTier;
    d->lockLayerCount = 1;
    double intensify = (double)rung / (double)cfg->escalationCap;
    d->intensify = intensify < 1.0 ? intensify : 1.0;
}

// The buildup's structural anchor: odd buildups ride the lament ground, even
// ones the dominant pedal; both off below rung 1.
static void ground(const AnoDramaturgConfig *cfg, const AnoLedger *l, AnoDirective *d)
{
    if (d->escalation < 1 || !cfg->earnedDissonance)
        return;
    if (cfg->lamentBass && l->buildups % 2 == 1) {
        d->lament = true;
        return;
    }
    d->pedal = 5; // the dominant: maximal cadential pull
}

static AnoDirective neutral(void)
{
    AnoDirective d = { 0 };
    d.cadence = ANO_CADENCE_NONE;
    return d;
}

static AnoDirective accrue(const AnoDramaturgConfig *cfg, AnoLedger *l, AnoPhrasePos pos)
{
    l->withholdingPhrases += 1;
    l->barsSinceAuthentic += pos.bars;
    if (l->barsSinceAuthentic > cfg->maxDebt)
        l->barsSinceAuthentic = cfg->maxDebt;
    l->deceptions += 1;
    double prev = l->hasPrevBaseTension ? l->prevBaseTension : 0.0;
    if (prev > l->peakTension)
        l->peakTension = prev;
    ano_ledger_set_cadence(l, pos.phrase, ANO_CADENCE_DECEPTIVE); // ration: refuse the tonic
    AnoDirective d = neutral();
    d.cadence = ANO_CADENCE_DECEPTIVE;
    d.withholdRootTonic = true;
    withholding(cfg, l, &d);
    ground(cfg, l, &d);
    return d;
}

static AnoDirective spend(const AnoDramaturgConfig *cfg, AnoLedger *l, AnoPhrasePos pos)
{
    double magnitude = ano_spend_magnitude(l, cfg);
    int brighten = magnitude >= cfg->bigSpend ? 2 : 1;
    ano_ledger_set_cadence(l, pos.phrase, ANO_CADENCE_AUTHENTIC);
    l->lastSpend = magnitude;
    l->barsSinceAuthentic = 0; // cashed — reset the ledger
    l->deceptions = 0;
    l->withholdingPhrases = 0;
    l->peakTension = 0.0;
    l->buildups += 1; // the next buildup alternates its ground
    AnoDirective d = neutral();
    d.cadence = ANO_CADENCE_AUTHENTIC;
    d.payoff = magnitude;
    d.brighten = brighten;
    return d;
}

// Mid-phrase carry: keep the withholding constraints while accruing.
static AnoDirective standing(const AnoDramaturgConfig *cfg, const AnoLedger *l)
{
    if (l->withholdingPhrases <= 0)
        return neutral();
    AnoDirective d = neutral();
    d.withholdRootTonic = true;
    withholding(cfg, l, &d);
    ground(cfg, l, &d);
    return d;
}

AnoDirective ano_dramaturg_on_bar(const AnoDramaturgConfig *cfg, AnoLedger *l,
                                  double baseTension, AnoPhrasePos pos)
{
    l->prevBaseTension = baseTension;
    l->hasPrevBaseTension = true;

    AnoDirective d;
    if (pos.pos != 0) {
        d = standing(cfg, l);
    } else {
        bool accruing = baseTension >= cfg->accrueAbove;
        bool releasing = baseTension < release_level(cfg) && ledger_debt(l) > 0;
        if (releasing && !accruing)
            d = spend(cfg, l, pos);
        else if (accruing)
            d = accrue(cfg, l, pos);
        else {
            l->withholdingPhrases = 0; // neutral zone: hand back to the mapper
            d = neutral();
        }
    }
    // persist the walk signals (the walk runs a bar ahead; skew tolerated)
    l->suppressTonic = d.withholdRootTonic;
    l->lament = d.lament;
    // M14: ornament a controlled cadence with a prepared suspension; on the
    // payoff cadence also permit the unprepared appoggiatura
    if (cfg->earnedDissonance) {
        int8_t controlled = ano_ledger_cadence(l, pos.phrase);
        AnoCadenceSlot slot = ano_phrase_slot(pos);
        if (controlled != ANO_CADENCE_NONE
            && (slot == ANO_SLOT_PRE_CADENCE || slot == ANO_SLOT_CADENCE))
            d.suspend = true;
        if (controlled == ANO_CADENCE_AUTHENTIC && slot == ANO_SLOT_CADENCE)
            d.appoggiatura = true;
    }
    return d;
}
