/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Public music API: curated config == private engine note-for-note; snapshot/restore/seek.
// Exit 0 == pass.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_music.h>

#include "music/music_conductor.h"

static int failures = 0;
#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            failures++;                                              \
        }                                                            \
    } while (0)

#define BARS 32u

static bool ev_eq(const AnoNoteEvent *a, const AnoNoteEvent *b)
{
    return a->start == b->start && a->dur == b->dur && a->pitch == b->pitch
           && a->velocity == b->velocity && a->layer == b->layer && a->tie == b->tie;
}

// Everything-on config in both vocabularies.
static void public_config(AnoMusicConfig *c)
{
    *c = ano_music_config_default();
    c->hasMapper = true;
    c->mapper = ano_mapping_table_default();
    c->hasDramaturg = true;
    c->dramaturg = ano_dramaturg_config_default();
    c->phraseGroove = true;
    c->cadenceRit = 0.02;
    c->wanderPhrases = 4;
    c->form.cadential64 = c->form.periods = c->form.hypermeter = true;
    c->form.bassInversions = c->form.split64 = true;
    c->texture.doubling = c->texture.animate = c->texture.imitation = true;
    c->texture.rotate = c->texture.counter = true;
    c->ties.anacrusis = c->ties.suspension = c->ties.syncopation = true;
    c->clock.codetta = c->clock.extension = c->clock.elision = true;
    c->melody.planApex = c->melody.counterpoint = true;
}

static void private_config(AnoEngineConfig *e)
{
    *e = ano_engine_config_default();
    e->hasMapper = true;
    e->mapper = ano_mapping_table_default();
    e->hasDramaturg = true;
    e->dramaturg = ano_dramaturg_config_default();
    e->phraseGroove = true;
    e->cadenceRit = 0.02;
    e->wanderPhrases = 4;
    e->form.cadential64 = e->form.periods = e->form.hypermeter = true;
    e->form.bassInversions = e->form.split64 = true;
    e->texture.doubling = e->texture.animate = e->texture.imitation = true;
    e->texture.rotate = e->texture.counter = true;
    e->ties.anacrusis = e->ties.suspension = e->ties.syncopation = true;
    e->clock.codetta = e->clock.extension = e->clock.elision = true;
    e->melody.planApex = e->melody.counterpoint = true;
}

int main(void)
{
    static AnoMusicEngine priv;
    static AnoBarResult pr;
    static AnoMusicBar pb;

    // --- public config == private, note for note ---
    for (int rich = 0; rich < 2; ++rich) {
        AnoMusicConfig pubCfg;
        AnoEngineConfig privCfg;
        if (rich) {
            public_config(&pubCfg);
            private_config(&privCfg);
        } else {
            pubCfg = ano_music_config_default();
            privCfg = ano_engine_config_default();
        }
        AnoMusicEngine *pub = ano_music_create(&pubCfg, 42);
        CHECK(pub != NULL, "ano_music_create");
        ano_engine_init(&priv, 42, &privCfg);

        uint32_t events = 0;
        bool same = true;
        for (uint32_t b = 0; b < BARS && same; ++b) {
            ano_music_advance_bar(pub, &pb);
            ano_engine_advance_bar(&priv, &pr);
            same = pb.meaning.bar == pr.bar && pb.eventCount == pr.eventCount
                   && pb.tempoCount == pr.tempoPointCount
                   && pb.params.tempoBpm == pr.params.tempoBpm
                   && pb.params.layersActive != 0u;
            for (uint32_t i = 0; i < pb.eventCount && same; ++i)
                same = ev_eq(&pb.events[i], &pr.events[i].core);
            events += pb.eventCount;
            if (!same)
                printf("  %s config: bar %u differs\n", rich ? "rich" : "default", b);
        }
        CHECK(same, rich ? "public config == private (everything on)"
                         : "public config == private (defaults)");
        CHECK(events > (rich ? 400u : 100u), "the piece has substance");
        ano_music_destroy(pub);
    }

    // --- bar MEANING populated (AEVT_MUSIC_BAR payload) ---
    {
        AnoMusicConfig cfg;
        public_config(&cfg);
        AnoMusicEngine *e = ano_music_create(&cfg, 42);
        uint32_t cadences = 0, keyArrivals = 0;
        int chordsSeen = 0;
        for (uint32_t b = 0; b < 64u; ++b) {
            ano_music_advance_bar(e, &pb);
            cadences += pb.meaning.isCadence;
            keyArrivals += pb.meaning.keyArrived;
            if (pb.meaning.chordDegree >= 1 && pb.meaning.chordDegree <= 7)
                chordsSeen++;
            CHECK(pb.meaning.keyTonic >= 0 && pb.meaning.keyTonic < 12, "key is a pitch class");
        }
        CHECK(cadences > 4u, "cadences are reported");
        CHECK(keyArrivals > 0u, "the wander's key arrivals are reported");
        CHECK(chordsSeen == 64, "every bar names its chord");
        ano_music_destroy(e);
    }

    // --- overrides by name; typo refused ---
    {
        AnoMusicConfig cfg;
        public_config(&cfg);
        AnoMusicEngine *e = ano_music_create(&cfg, 42);
        CHECK(ano_music_set_override(e, "reverb_send", 0.5), "override accepted");
        CHECK(!ano_music_set_override(e, "revreb_send", 0.5), "typo refused");
        ano_music_advance_bar(e, &pb);
        CHECK(pb.params.reverbSend == 0.5f, "the override is in force");
        ano_music_clear_override(e, "reverb_send");
        ano_music_advance_bar(e, &pb);
        CHECK(pb.params.reverbSend != 0.5f, "clearing returns it to the mapper");
        ano_music_destroy(e);
    }

    // --- snapshot/restore: engine bytes are the future ---
    {
        AnoMusicConfig cfg;
        public_config(&cfg);
        size_t sz = ano_music_snapshot_size();
        CHECK(sz == sizeof(AnoMusicEngine), "snapshot is the engine");
        void *snap = malloc(sz);
        void *snapB = malloc(sz);

        AnoMusicEngine *e = ano_music_create(&cfg, 42);
        for (uint32_t b = 0; b < 20u; ++b)
            ano_music_advance_bar(e, &pb);
        CHECK(ano_music_snapshot(e, snap, sz), "snapshot at bar 20");

        // Record future.
        static AnoMusicBar future[12];
        for (uint32_t b = 0; b < 12u; ++b)
            ano_music_advance_bar(e, &future[b]);

        // Restore and re-run: same future.
        CHECK(ano_music_restore(e, snap, sz), "restore");
        bool same = true;
        for (uint32_t b = 0; b < 12u && same; ++b) {
            ano_music_advance_bar(e, &pb);
            same = pb.meaning.bar == future[b].meaning.bar && pb.eventCount == future[b].eventCount;
            for (uint32_t i = 0; i < pb.eventCount && same; ++i)
                same = ev_eq(&pb.events[i], &future[b].events[i]);
        }
        CHECK(same, "restore reproduces the future exactly");

        // Seek: fresh engine fast-forwarded to bar 20 == snapshot.
        AnoMusicEngine *fresh = ano_music_create(&cfg, 42);
        for (uint32_t b = 0; b < 20u; ++b)
            ano_music_advance_bar(fresh, &pb);
        CHECK(ano_music_snapshot(fresh, snapB, sz), "snapshot the rebuild");
        if (memcmp(snap, snapB, sz) != 0) {
            const unsigned char *x = snap, *y = snapB;
            size_t first = sz, n = 0;
            for (size_t i = 0; i < sz; ++i)
                if (x[i] != y[i]) { if (first == sz) first = i; n++; }
            // Indeterminate padding in config defaults is the usual culprit.
            printf("  %zu of %zu bytes differ, first at offset %zu "
                   "(config spans 0..%zu) 〜 indeterminate padding?\n",
                   n, sz, first, sizeof(AnoEngineConfig));
        }
        CHECK(memcmp(snap, snapB, sz) == 0,
              "a rebuilt engine is byte-identical to the snapshot");

        // Different bar -> different snapshot.
        for (uint32_t b = 0; b < 2u; ++b)
            ano_music_advance_bar(fresh, &pb);
        ano_music_snapshot(fresh, snapB, sz);
        CHECK(memcmp(snap, snapB, sz) != 0, "a different bar is a different state");

        free(snap);
        free(snapB);
        ano_music_destroy(e);
        ano_music_destroy(fresh);
    }

    if (failures) {
        printf("anotest_musichost: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musichost: all passed\n");
    return 0;
}
