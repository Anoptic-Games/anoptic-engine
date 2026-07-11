/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_modifiers.h (private to src/music/)
 * Composable IR -> IR modifiers (musicgen/modifiers/__init__.py). Each is
 * pure given its inputs; all randomness comes from the caller's per-(layer,
 * bar) stream. Chain order matters: slot-based modifiers (Accent) run before
 * time-movers (Swing, Humanize); Perform sits after Articulate (legato must
 * not refill the luftpause) and before Humanize's jitter. Tie halves keep
 * their joins: no time jitter on any tied event, no velocity jitter or strum
 * stagger on continuations, no echo of chain halves, no gate on "out"/"both".
 */

#ifndef ANO_MUSIC_MODIFIERS_H
#define ANO_MUSIC_MODIFIERS_H

#include "music_ir.h"

#define ANO_MOD_MIN_DUR 0.1 // beats; floor for any duration-shrinking modifier

typedef enum AnoModKind
{
    ANO_MOD_SWING = 0,
    ANO_MOD_HUMANIZE,
    ANO_MOD_ARTICULATE,
    ANO_MOD_ACCENT,
    ANO_MOD_PERFORM,
    ANO_MOD_ECHO,
    ANO_MOD_STRUM,
    ANO_MOD_TRANSPOSE,
} AnoModKind;

typedef struct AnoModifier
{
    AnoModKind kind;
    union {
        struct { double amount; } swing;              // 0.5
        struct { double tSigma, vSigma; } humanize;   // 0.015, 5.0
        struct { bool hasGate; double gate; } articulate;   // None -> articulation
        struct { bool hasDepth; double depth; } accent;     // None -> accentDepth
        struct { double hairpin, contour, agogic, luftpause, lag; } perform;
        struct { double delay, decay; int repeats, minVelocity; } echo;
        struct { double spread; } strum;              // 0.05
        struct { int octaves, steps; } transpose;
    } u;
} AnoModifier;

// Constructors with the prototype's defaults.
AnoModifier ano_mod_swing(double amount);
AnoModifier ano_mod_humanize(double tSigma, double vSigma);
AnoModifier ano_mod_articulate(void);            // gate <- params.articulation
AnoModifier ano_mod_articulate_gate(double gate);
AnoModifier ano_mod_accent(void);                // depth <- params.accentDepth
AnoModifier ano_mod_accent_depth(double depth);
AnoModifier ano_mod_perform(double hairpin, double contour, double agogic, double lag);
AnoModifier ano_mod_echo(void); // delay 0.75, decay 0.55, repeats 2, min 24
AnoModifier ano_mod_strum(double spread);
AnoModifier ano_mod_transpose(int octaves, int steps);

// One modifier over an event list, in place with growth room (Echo appends).
// Returns the new count; cap bounds the buffer.
uint32_t ano_mod_apply(const AnoModifier *mod, AnoMusicEvent *events,
                       uint32_t count, uint32_t cap,
                       const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoMusicRng *rng);

uint32_t ano_apply_chain(const AnoModifier *chain, uint32_t chainLen,
                         AnoMusicEvent *events, uint32_t count, uint32_t cap,
                         const AnoHarmonicContext *ctx, AnoMeter meter,
                         const AnoGenParams *params, AnoMusicRng *rng);

// PLANS.md section 7 default chains; perform=true inserts the A1 shaping per
// pitched layer. Returns the chain length (<= 4).
uint32_t ano_default_chain(uint8_t layer, bool perform, AnoModifier out[4]);

#endif // ANO_MUSIC_MODIFIERS_H
