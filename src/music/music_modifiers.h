/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Composable IR -> IR modifiers. Randomness from caller's per-(layer, bar) stream.
// Chain: slot mods (Accent) before time-movers (Swing, Humanize). Perform after Articulate, before Humanize.
// Tied events: no time/velocity jitter on joins, no echo of chain halves, no gate on out/both.

#ifndef ANO_MUSIC_MODIFIERS_H
#define ANO_MUSIC_MODIFIERS_H

#include "music_ir.h"

#define ANO_MOD_MIN_DUR 0.1 // beats; floor for duration-shrinking mods

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
        struct { bool hasGate; double gate; } articulate;
        struct { bool hasDepth; double depth; } accent;
        struct { double hairpin, contour, agogic, luftpause, lag; } perform;
        struct { double delay, decay; int repeats, minVelocity; } echo;
        struct { double spread; } strum;              // 0.05
        struct { int octaves, steps; } transpose;
    } u;
} AnoModifier;

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

uint32_t ano_mod_apply(const AnoModifier *mod, AnoMusicEvent *events,
                       uint32_t count, uint32_t cap,
                       const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoMusicRng *rng);

uint32_t ano_apply_chain(const AnoModifier *chain, uint32_t chainLen,
                         AnoMusicEvent *events, uint32_t count, uint32_t cap,
                         const AnoHarmonicContext *ctx, AnoMeter meter,
                         const AnoGenParams *params, AnoMusicRng *rng);

// Default chains. perform=true inserts A1 shaping per pitched layer. Returns length (<= 4).
uint32_t ano_default_chain(uint8_t layer, bool perform, AnoModifier out[4]);

#endif // ANO_MUSIC_MODIFIERS_H
