/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * ano_synth.c
 * Synth lifecycle, score loading (merge_ties + BeatClock + the frame-stamped
 * schedule), the transport, the generator, and the console helpers.
 * Public contract: include/anoptic_synth.h.
 *
 * Scheduling semantics ported exactly (TECH_SPEC §11.3): the FULL tempo map is
 * built before any event is placed (merged notes span bars); schedule order is
 * (frame, kind, seq) with params < note at equal frames; the generator steps
 * sub-block spans that stop at every bar edge and note onset, so event timing
 * is sample-accurate in both the realtime and offline paths — which closes the
 * prototype's documented realtime tie-rearticulation gap (a merged chain is
 * one voice with one envelope everywhere).
 */

#include "synth_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_logging.h>

// ---------------------------------------------------------------------------
// Patch registry
// ---------------------------------------------------------------------------

static const char *const PATCH_NAMES[ANO_SYNTH_PATCH_COUNT] = {
    "", "warm", "bright", "morph", "breeze", "round", "driven", "bad_ground",
    "soft", "hard", "mellow", "keys", "whistle", "pluck", "glass", "chimes",
};

uint32_t ano_synth_patch_id(const char *name)
{
    if (!name)
        return 0;
    for (uint32_t i = 1; i < ANO_SYNTH_PATCH_COUNT; ++i)
        if (strcmp(name, PATCH_NAMES[i]) == 0)
            return i;
    return 0;
}

const char *ano_synth_patch_name(uint32_t id)
{
    return id < ANO_SYNTH_PATCH_COUNT ? PATCH_NAMES[id] : "";
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AnoSynth *ano_synth_create(const AnoSynthDesc *desc)
{
    AnoSynthDesc d = desc ? *desc : (AnoSynthDesc){0};
    uint32_t rate   = d.sampleRate ? d.sampleRate : 48000u;
    uint32_t voices = d.maxVoices ? d.maxVoices : 96u;

    mi_heap_t *heap = mi_heap_new();
    if (!heap)
        return NULL;
    AnoSynth *s = mi_heap_calloc(heap, 1, sizeof *s);
    if (!s) {
        mi_heap_destroy(heap);
        return NULL;
    }
    s->heap       = heap;
    s->sampleRate = rate;
    s->maxVoices  = voices;
    s->smoothCoef = expf(-1.0f / (0.030f * (float)rate));
    atomic_init(&s->startFrame, ANO_SYNTH_IDLE);

    s->voices   = mi_heap_calloc(heap, voices, sizeof *s->voices);
    s->duckGain = mi_heap_calloc(heap, ANO_SYNTH_SPAN_MAX, sizeof(float));
    s->wtBank   = mi_heap_calloc(heap, (size_t)ANO_SYNTH_WT_FRAMES * ANO_SYNTH_WT_LEN,
                                 sizeof(float));
    s->bellFrames = (uint64_t)(1.6f * (float)rate);
    s->bell       = mi_heap_calloc(heap, s->bellFrames, sizeof(float));

    // shimmer history: 2 s rounded up to a power of two
    uint32_t cap = 1;
    while (cap < rate * 2u)
        cap <<= 1;
    s->grainCap  = cap;
    s->grainRing = mi_heap_calloc(heap, cap, sizeof(float));

    if (!s->voices || !s->duckGain || !s->wtBank || !s->bell || !s->grainRing) {
        mi_heap_destroy(heap);
        return NULL;
    }
    ano_synth_bake_wavetable(s->wtBank);
    ano_synth_bake_bell(s->bell, s->bellFrames, (float)rate);
    return s;
}

void ano_synth_destroy(AnoSynth *s)
{
    if (s)
        mi_heap_destroy(s->heap);
}

uint32_t ano_synth_dropped(const AnoSynth *s)
{
    return s->dropped;
}

// ---------------------------------------------------------------------------
// BeatClock (TECH_SPEC §11.1, ported verbatim)
// ---------------------------------------------------------------------------

// Ring accessors. Batch masks are UINT32_MAX, so these are plain indexing.
static AnoSynthAnchor *anchor_at(const AnoSynth *s, uint32_t i)
{
    return &s->anchors[i & s->anchorMask];
}
static AnoSynthBar *bar_at(const AnoSynth *s, uint32_t i)
{
    return &s->bars[i & s->barMask];
}
static AnoSynthNote *note_at(const AnoSynth *s, uint32_t i)
{
    return &s->notes[i & s->noteMask];
}

// The oldest anchor still addressable. Live drops anchors off the back of the
// ring; every anchor carries its own ABSOLUTE time, so the truncated clock is
// still exact for any beat at or after the oldest retained one — and the only
// beats ever queried are at the playhead or ahead of it.
static uint32_t anchor_floor(const AnoSynth *s)
{
    return s->live && s->anchorCount > s->anchorCap ? s->anchorCount - s->anchorCap
                                                    : 0u;
}

// A point at the last anchor's beat replaces its bpm; a point before it is an
// error; otherwise the new anchor's time extends at the PREVIOUS anchor's bpm.
static bool clock_add(AnoSynth *s, double beat, double bpm)
{
    AnoSynthAnchor *last = anchor_at(s, s->anchorCount - 1u);
    if (beat < last->beat - 1e-9)
        return false;
    if (fabs(beat - last->beat) < 1e-9) {
        last->bpm = bpm;
        return true;
    }
    if (!s->live && s->anchorCount >= s->anchorCap)
        return false;
    AnoSynthAnchor add = { beat, last->time + (beat - last->beat) * 60.0 / last->bpm,
                           bpm };
    *anchor_at(s, s->anchorCount) = add; // live: retires the oldest
    s->anchorCount++;
    return true;
}

// Last anchor at-or-before `beat`, extrapolated at that anchor's bpm.
static double clock_time_at(const AnoSynth *s, double beat)
{
    uint32_t floor_ = anchor_floor(s);
    for (uint32_t i = s->anchorCount; i-- > floor_;) {
        const AnoSynthAnchor *a = anchor_at(s, i);
        if (beat >= a->beat - 1e-9)
            return a->time + (beat - a->beat) * 60.0 / a->bpm;
    }
    const AnoSynthAnchor *a = anchor_at(s, floor_);
    return a->time + (beat - a->beat) * 60.0 / a->bpm;
}

// ---------------------------------------------------------------------------
// Score loading
// ---------------------------------------------------------------------------

bool ano_synth_score_begin(AnoSynth *s, double barQuarters, uint32_t barCount,
                           uint32_t tempoCount, uint32_t eventCount)
{
    if (barQuarters <= 0.0 || barCount == 0u)
        return false;
    if (atomic_load_explicit(&s->startFrame, memory_order_acquire) != ANO_SYNTH_IDLE)
        return false; // loading while the generator runs is a contract breach
    mi_free(s->anchors);
    mi_free(s->bars);
    mi_free(s->raw);
    mi_free(s->notes);
    s->live        = false;
    s->barQuarters = barQuarters;
    s->anchorCap   = tempoCount + 1u;
    s->barCap      = barCount;
    s->rawCap      = eventCount;
    s->noteCap     = eventCount;
    s->anchorMask = s->barMask = s->noteMask = UINT32_MAX; // batch: plain arrays
    s->anchors = mi_heap_calloc(s->heap, s->anchorCap, sizeof *s->anchors);
    s->bars    = mi_heap_calloc(s->heap, s->barCap, sizeof *s->bars);
    s->raw     = eventCount ? mi_heap_calloc(s->heap, s->rawCap, sizeof *s->raw) : NULL;
    s->notes   = eventCount ? mi_heap_calloc(s->heap, s->rawCap, sizeof *s->notes) : NULL;
    if (!s->anchors || !s->bars || (eventCount && (!s->raw || !s->notes)))
        return false;
    s->anchors[0]   = (AnoSynthAnchor){ 0.0, 0.0, 100.0 };
    s->anchorCount  = 1;
    s->barCount     = 0;
    s->rawCount     = 0;
    s->noteCount    = 0;
    s->lastNoteEnd  = 0;
    s->scoreReady   = false;
    return true;
}

bool ano_synth_score_tempo(AnoSynth *s, double beat, double bpm)
{
    if (bpm <= 0.0)
        return false;
    return clock_add(s, beat, bpm);
}

bool ano_synth_score_bar(AnoSynth *s, uint32_t bar, const AnoMusicalParams *p,
                         const AnoMusicAffect *a)
{
    if (!p || !a || s->barCount >= s->barCap || bar != s->barCount)
        return false;
    AnoSynthBar *b = &s->bars[s->barCount++];
    b->params = *p;
    b->affect = *a;
    b->frame = 0;      // stamped in score_end, after the clock is complete
    b->barSeconds = 0;
    return true;
}

bool ano_synth_score_event(AnoSynth *s, const AnoNoteEvent *ev)
{
    if (!ev || s->rawCount >= s->rawCap || ev->dur <= 0.0
        || ev->layer >= ANO_MUSIC_LAYER_COUNT || ev->velocity == 0u)
        return false;
    s->raw[s->rawCount++] = *ev;
    return true;
}

// Python round(x, 10): scale, round half to even, unscale.
static double round10(double x)
{
    return nearbyint(x * 1e10) / 1e10;
}

// merge_ties, exact semantics (TECH_SPEC §4.2): chains keyed (layer, pitch);
// an in/both event continues an open chain iff the head's end meets its start
// within 1e-9 (in also closes it); out/both becomes a head — a copy with tie
// cleared; everything else, including an orphan in, passes through in order.
// Writes merged events into notes[].ev; returns the merged count.
static uint32_t merge_ties(AnoSynth *s)
{
    int32_t open[ANO_MUSIC_LAYER_COUNT][128];
    memset(open, 0xFF, sizeof open); // all -1
    uint32_t m = 0;
    for (uint32_t i = 0; i < s->rawCount; ++i) {
        const AnoNoteEvent *ev = &s->raw[i];
        int32_t *slot = &open[ev->layer][ev->pitch & 0x7F];
        if ((ev->tie == ANO_MUSIC_TIE_IN || ev->tie == ANO_MUSIC_TIE_BOTH) && *slot >= 0) {
            AnoNoteEvent *head = &s->notes[*slot].ev;
            if (fabs(head->start + head->dur - ev->start) < 1e-9) {
                head->dur = round10(head->dur + ev->dur);
                if (ev->tie == ANO_MUSIC_TIE_IN)
                    *slot = -1;
                continue;
            }
        }
        s->notes[m].ev = *ev;
        if (ev->tie == ANO_MUSIC_TIE_OUT || ev->tie == ANO_MUSIC_TIE_BOTH) {
            s->notes[m].ev.tie = ANO_MUSIC_TIE_NONE;
            *slot = (int32_t)m;
        }
        ++m;
    }
    return m;
}

static int note_cmp(const void *a, const void *b)
{
    const AnoSynthNote *x = a, *y = b;
    if (x->frame != y->frame)
        return x->frame < y->frame ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq ? 1 : 0;
}

bool ano_synth_score_end(AnoSynth *s)
{
    if (s->barCount == 0u)
        return false;
    double fs = (double)s->sampleRate;

    s->noteCount = merge_ties(s);
    for (uint32_t i = 0; i < s->noteCount; ++i) {
        AnoSynthNote *n = &s->notes[i];
        double on  = clock_time_at(s, n->ev.start);
        double off = clock_time_at(s, n->ev.start + n->ev.dur);
        n->frame = (uint64_t)(on * fs);
        n->seq   = i;
        n->durS  = (float)(off - on);
        uint64_t end = (uint64_t)(off * fs);
        if (end > s->lastNoteEnd)
            s->lastNoteEnd = end;
    }
    qsort(s->notes, s->noteCount, sizeof *s->notes, note_cmp);

    for (uint32_t b = 0; b < s->barCount; ++b) {
        AnoSynthBar *bar = &s->bars[b];
        bar->frame = (uint64_t)(clock_time_at(s, (double)b * s->barQuarters) * fs);
        bar->barSeconds = (float)(s->barQuarters * 60.0 / bar->params.tempoBpm);
    }
    s->scoreReady = true;
    return true;
}

uint64_t ano_synth_score_frames(const AnoSynth *s, float tailSeconds)
{
    if (!s->scoreReady || s->live)
        return 0; // a live score has no end
    return s->lastNoteEnd + (uint64_t)(tailSeconds * (float)s->sampleRate);
}

double ano_synth_time_at(const AnoSynth *s, double beat)
{
    return s->scoreReady ? clock_time_at(s, beat) : 0.0;
}

// ---------------------------------------------------------------------------
// Live scoring: the same schedule, built one bar at a time
// ---------------------------------------------------------------------------

#define LIVE_NOTES   1024u // ring capacities (powers of two)
#define LIVE_BARS      16u
#define LIVE_ANCHORS  256u

bool ano_synth_live_begin(AnoSynth *s, double barQuarters)
{
    if (barQuarters <= 0.0)
        return false;
    if (atomic_load_explicit(&s->startFrame, memory_order_acquire) != ANO_SYNTH_IDLE)
        return false; // same contract as score_begin
    mi_free(s->anchors);
    mi_free(s->bars);
    mi_free(s->raw);
    mi_free(s->notes);
    s->raw = NULL;
    s->rawCount = s->rawCap = 0;

    s->live        = true;
    s->barQuarters = barQuarters;
    s->anchorCap   = LIVE_ANCHORS;
    s->barCap      = LIVE_BARS;
    s->noteCap     = LIVE_NOTES;
    s->anchorMask  = LIVE_ANCHORS - 1u;
    s->barMask     = LIVE_BARS - 1u;
    s->noteMask    = LIVE_NOTES - 1u;
    s->anchors = mi_heap_calloc(s->heap, s->anchorCap, sizeof *s->anchors);
    s->bars    = mi_heap_calloc(s->heap, s->barCap, sizeof *s->bars);
    s->notes   = mi_heap_calloc(s->heap, s->noteCap, sizeof *s->notes);
    if (!s->anchors || !s->bars || !s->notes)
        return false;

    s->anchors[0]  = (AnoSynthAnchor){ 0.0, 0.0, 100.0 };
    s->anchorCount = 1;
    s->barCount    = 0;
    s->noteCount   = 0;
    s->lastNoteEnd = 0;
    s->liveNextBar = 0;
    s->liveLate = s->liveOverflow = 0;
    memset(s->openChain, 0xFF, sizeof s->openChain); // all -1
    s->scoreReady = true; // live has no separate "end"
    return true;
}

// The onset frame, from the clock in force. The bar's tempo points are added
// before its events, so the ONSET is always knowable here. The sounding
// DURATION is not — the note's end may reach into a bar that has not arrived —
// so it is finalized at spawn (synth_spawn_note).
static void live_stamp(AnoSynth *s, AnoSynthNote *n)
{
    n->frame = (uint64_t)(clock_time_at(s, n->ev.start) * (double)s->sampleRate);
    n->durS  = 0.0f;
}

// The appended run [from, noteCount) is sorted within itself (the generator
// emits by (start, pitch)), but an echo can spill past the next bar's downbeat,
// so the run may overlap the tail already queued. Insertion-sort it back into
// place — stable, and never behind the cursor (a note there has already sounded).
// Equal frames keep append order, which IS the batch path's `seq` order.
static void live_order(AnoSynth *s, uint32_t from)
{
    for (uint32_t i = from; i < s->noteCount; ++i) {
        uint32_t j = i;
        while (j > s->noteCursor && note_at(s, j - 1u)->frame > note_at(s, j)->frame) {
            AnoSynthNote t = *note_at(s, j - 1u);
            *note_at(s, j - 1u) = *note_at(s, j);
            *note_at(s, j) = t;
            --j;
        }
    }
}

bool ano_synth_live_bar(AnoSynth *s, uint32_t bar,
                        const AnoTempoPoint *tempo, uint32_t tempoCount,
                        const AnoMusicalParams *p, const AnoMusicAffect *a,
                        const AnoNoteEvent *events, uint32_t eventCount)
{
    if (!s->live || !p || !a || bar != s->liveNextBar)
        return false;
    if (s->barCount - s->barCursor >= s->barCap)
        return false; // the bar ring is full: the driver ran too far ahead

    // tempo first: every frame stamp below reads the clock
    for (uint32_t i = 0; i < tempoCount; ++i)
        if (!clock_add(s, tempo[i].beat, tempo[i].bpm))
            return false;

    AnoSynthBar *b = bar_at(s, s->barCount);
    b->params     = *p;
    b->affect     = *a;
    b->frame      = (uint64_t)(clock_time_at(s, (double)bar * s->barQuarters)
                          * (double)s->sampleRate);
    b->barSeconds = (float)(s->barQuarters * 60.0 / p->tempoBpm);
    s->barCount++;

    // merge_ties, incrementally: the open chains persist across the barline
    uint32_t from = s->noteCount;
    for (uint32_t i = 0; i < eventCount; ++i) {
        const AnoNoteEvent *ev = &events[i];
        if (ev->dur <= 0.0 || ev->layer >= ANO_MUSIC_LAYER_COUNT || ev->velocity == 0u)
            continue;
        int32_t *slot = &s->openChain[ev->layer][ev->pitch & 0x7F];
        if (ev->tie == ANO_MUSIC_TIE_IN || ev->tie == ANO_MUSIC_TIE_BOTH) {
            if (*slot >= 0 && (uint32_t)*slot >= s->noteCursor) {
                AnoSynthNote *head = note_at(s, (uint32_t)*slot);
                if (fabs(head->ev.start + head->ev.dur - ev->start) < 1e-9) {
                    head->ev.dur = round10(head->ev.dur + ev->dur);
                    if (ev->tie == ANO_MUSIC_TIE_IN)
                        *slot = -1; // the chain closed
                    continue;
                }
            } else if (*slot >= 0) {
                // the head already sounded — it cannot grow now, so the
                // continuation stands alone (the orphan the linter licenses)
                s->liveLate++;
                *slot = -1;
            }
        }
        if (s->noteCount - s->noteCursor >= s->noteCap) {
            s->liveOverflow++;
            continue;
        }
        AnoSynthNote *n = note_at(s, s->noteCount);
        n->ev  = *ev;
        n->seq = s->noteCount;
        if (ev->tie == ANO_MUSIC_TIE_OUT || ev->tie == ANO_MUSIC_TIE_BOTH) {
            n->ev.tie = ANO_MUSIC_TIE_NONE;
            *slot = (int32_t)s->noteCount;
        }
        live_stamp(s, n);
        s->noteCount++;
    }
    live_order(s, from);
    s->liveNextBar = bar + 1u;
    return true;
}

uint32_t ano_synth_live_pending(const AnoSynth *s, uint64_t worldFrame)
{
    if (!s->live)
        return 0;
    uint64_t t0 = atomic_load_explicit(&s->startFrame, memory_order_acquire);
    if (t0 == ANO_SYNTH_IDLE)
        return s->barCount - s->barCursor; // nothing has sounded yet
    uint64_t scoreF = worldFrame > t0 ? worldFrame - t0 : 0u;
    uint32_t n = 0;
    for (uint32_t i = s->barCursor; i < s->barCount; ++i)
        if (bar_at(s, i)->frame > scoreF)
            n++;
    return n;
}

uint32_t ano_synth_live_late(const AnoSynth *s)
{
    return s->liveLate;
}

uint32_t ano_synth_live_overflow(const AnoSynth *s)
{
    return s->liveOverflow;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void ano_synth_transport_start(AnoSynth *s, uint64_t worldFrame)
{
    // full runtime reset first; the release store publishes it to the generator
    memset(s->voices, 0, (size_t)s->maxVoices * sizeof *s->voices);
    s->noteCursor = s->barCursor = 0;
    s->dropped    = 0;
    ano_audio_smooth_snap(&s->cutoff, 2500.0f);
    ano_audio_smooth_snap(&s->duckDepth, 0.0f);
    ano_audio_smooth_snap(&s->shimGain, 0.0f);
    s->cutoff.coef = s->duckDepth.coef = s->shimGain.coef = s->smoothCoef;
    s->lfoPhase      = 0.0f;
    s->sweepVal      = 0.0f;
    s->lastBarCutoff = 2500.0f;
    memset(s->instruments, 0, sizeof s->instruments);
    memset(s->sweeps, 0, sizeof s->sweeps);
    memset(s->ducks, 0, sizeof s->ducks);
    memset(s->duckLive, 0, sizeof s->duckLive);
    memset(s->grainRing, 0, (size_t)s->grainCap * sizeof(float));
    ano_dsp_grain_init(&s->grain, s->grainRing, s->grainCap,
                       0.2f, 2.0f, 0.12f, 2.0f, 0x5EEDu);
    atomic_store_explicit(&s->startFrame, worldFrame, memory_order_release);
}

void ano_synth_transport_stop(AnoSynth *s)
{
    atomic_store_explicit(&s->startFrame, ANO_SYNTH_IDLE, memory_order_release);
}

// ---------------------------------------------------------------------------
// The generator
// ---------------------------------------------------------------------------

static void synth_apply_bar(AnoSynth *s, const AnoSynthBar *bar)
{
    const AnoMusicalParams *p = &bar->params;
    float cutoff = fmaxf(120.0f, p->filterCutoff);

    // one-shot audio-rate cutoff sweep on a large upward retarget (>= 1.6x):
    // bar-long attack, 1.5-bar release, summing, immutable once born
    for (uint32_t i = 0; i < ANO_SYNTH_MAX_SWEEPS; ++i)
        if (s->sweeps[i].barsLeft > 0)
            s->sweeps[i].barsLeft--;
    if (cutoff > s->lastBarCutoff * 1.6f && bar->barSeconds > 0.0f) {
        for (uint32_t i = 0; i < ANO_SYNTH_MAX_SWEEPS; ++i) {
            if (s->sweeps[i].barsLeft > 0)
                continue;
            ano_dsp_asr_init(&s->sweeps[i].env, bar->barSeconds * 0.9f, 0.0f,
                             bar->barSeconds * 1.5f, 1.5f, (float)s->sampleRate);
            s->sweeps[i].barsLeft = 6;
            break;
        }
    }
    s->lastBarCutoff = cutoff;

    s->cutoff.target    = cutoff;
    s->duckDepth.target = 0.4f * bar->affect.energy * bar->affect.energy;
    s->shimGain.target  = 0.35f * bar->affect.tension * bar->affect.tension;
    s->grain.density    = 2.0f + bar->affect.tension * 14.0f;
    memcpy(s->instruments, p->instruments, sizeof s->instruments);
}

static void synth_spawn_note(AnoSynth *s, AnoSynthNote *n)
{
    if (s->live) {
        // A note's END may lie in a bar whose tempo points had not arrived when
        // the note was appended — a long pad note, or a tie. The lookahead means
        // the clock covers it BY NOW, so this is the first moment the sounding
        // duration is knowable, and it is the value the batch path computes with
        // its complete map.
        double on  = clock_time_at(s, n->ev.start);
        double off = clock_time_at(s, n->ev.start + n->ev.dur);
        n->durS = (float)(off - on);
    }
    AnoSynthVoice *v = NULL;
    for (uint32_t i = 0; i < s->maxVoices; ++i) {
        if (!s->voices[i].active) {
            v = &s->voices[i];
            break;
        }
    }
    if (!v) {
        s->dropped++;
        return;
    }
    if (!ano_synth_voice_spawn(s, v, n)) {
        s->dropped++;
        return;
    }
    // each kick spawns one duck one-shot; overlapping kicks sum (flams pump deeper)
    if (n->ev.layer == ANO_MUSIC_PERC && n->ev.pitch == 36u) {
        for (uint32_t i = 0; i < ANO_SYNTH_MAX_DUCKS; ++i) {
            if (s->duckLive[i])
                continue;
            ano_dsp_asr_init(&s->ducks[i], 0.001f, 0.02f, 0.28f, 2.0f, (float)s->sampleRate);
            s->duckLive[i] = true;
            break;
        }
    }
}

static void synth_render_span(AnoSynth *s, float *const *busMix, uint32_t pos, uint32_t span)
{
    float fs = (float)s->sampleRate;

    // shared cutoff bus at span start: smoothed target x slow-LFO mod x sweeps
    float lfo = 1.0f + sinf(ANO_DSP_TWO_PI * s->lfoPhase) * 0.12f;
    float sw  = s->sweepVal > 1.0f ? 1.0f : s->sweepVal;
    float cutoffOut = s->cutoff.y * lfo * (1.0f + sw * 0.9f);
    float staged[ANO_MUSIC_LAYER_COUNT];
    staged[ANO_MUSIC_PAD]     = cutoffOut * 0.8f;
    staged[ANO_MUSIC_BASS]    = cutoffOut * 0.6f + 120.0f;
    staged[ANO_MUSIC_MELODY]  = cutoffOut;
    staged[ANO_MUSIC_COUNTER] = cutoffOut;
    staged[ANO_MUSIC_ARP]     = cutoffOut;
    staged[ANO_MUSIC_PERC]    = cutoffOut; // percussion never reads it

    // shared per-sample controls: advance smoothers/LFO/sweeps, fill duck gains
    for (uint32_t n = 0; n < span; ++n) {
        ano_audio_smooth_step(&s->cutoff);
        ano_audio_smooth_step(&s->shimGain);
        s->lfoPhase += 0.11f / fs;
        s->lfoPhase -= floorf(s->lfoPhase);
        float sweep = 0.0f;
        for (uint32_t i = 0; i < ANO_SYNTH_MAX_SWEEPS; ++i)
            if (s->sweeps[i].barsLeft > 0)
                sweep += ano_dsp_asr_step(&s->sweeps[i].env);
        s->sweepVal = sweep;
        float duck = 0.0f;
        for (uint32_t i = 0; i < ANO_SYNTH_MAX_DUCKS; ++i) {
            if (!s->duckLive[i])
                continue;
            duck += ano_dsp_asr_step(&s->ducks[i]);
            if (ano_dsp_asr_done(&s->ducks[i]))
                s->duckLive[i] = false;
        }
        if (duck > 1.0f) duck = 1.0f;
        float depth = ano_audio_smooth_step(&s->duckDepth);
        s->duckGain[n] = 1.0f - duck * depth;
    }

    // voices into their layer strips (pad + arp duck under kicks)
    for (uint32_t i = 0; i < s->maxVoices; ++i) {
        AnoSynthVoice *v = &s->voices[i];
        if (!v->active)
            continue;
        ano_synth_voice_span_coef(s, v, staged);
        float *strip = busMix[ANO_SYNTH_BUS_STRIP0 + v->layer];
        bool ducked = v->layer == ANO_MUSIC_PAD || v->layer == ANO_MUSIC_ARP;
        for (uint32_t n = 0; n < span; ++n) {
            if (v->age >= v->total) {
                v->active = false;
                break;
            }
            float l = 0.0f, r = 0.0f;
            ano_synth_voice_step(s, v, staged, &l, &r);
            if (ducked) {
                l *= s->duckGain[n];
                r *= s->duckGain[n];
            }
            strip[2u * (pos + n)]      += l;
            strip[2u * (pos + n) + 1u] += r;
            v->age++;
        }
    }

    // shimmer lane: granulate the pad strip's history an octave up
    const float *pad = busMix[ANO_SYNTH_BUS_STRIP0 + ANO_MUSIC_PAD];
    float *shim = busMix[ANO_SYNTH_BUS_SHIMMER];
    float g = s->shimGain.y;
    if (g > 1.0f) g = 1.0f;
    for (uint32_t n = 0; n < span; ++n) {
        float feed = 0.5f * (pad[2u * (pos + n)] + pad[2u * (pos + n) + 1u]);
        float l = 0.0f, r = 0.0f;
        ano_dsp_grain_step(&s->grain, feed, fs, &l, &r);
        shim[2u * (pos + n)]      += l * g;
        shim[2u * (pos + n) + 1u] += r * g;
    }
}

void ano_synth_generator(void *user, float *const *busMix, uint32_t busCount,
                         uint32_t frames, uint64_t startFrame)
{
    AnoSynth *s = user;
    uint64_t t0 = atomic_load_explicit(&s->startFrame, memory_order_acquire);
    if (t0 == ANO_SYNTH_IDLE || !s->scoreReady || busCount < ANO_SYNTH_CONSOLE_BUSES)
        return;
    if (startFrame + frames <= t0)
        return;
    uint32_t pos = t0 > startFrame ? (uint32_t)(t0 - startFrame) : 0u;
    uint64_t scoreF = startFrame + pos - t0;

    while (pos < frames) {
        // schedule priority at equal frames: params, then notes (kind order)
        while (s->barCursor < s->barCount && bar_at(s, s->barCursor)->frame <= scoreF)
            synth_apply_bar(s, bar_at(s, s->barCursor++));
        while (s->noteCursor < s->noteCount
               && note_at(s, s->noteCursor)->frame <= scoreF)
            synth_spawn_note(s, note_at(s, s->noteCursor++));

        uint64_t next = UINT64_MAX;
        if (s->barCursor < s->barCount)
            next = bar_at(s, s->barCursor)->frame;
        if (s->noteCursor < s->noteCount && note_at(s, s->noteCursor)->frame < next)
            next = note_at(s, s->noteCursor)->frame;

        uint32_t span = frames - pos;
        if (next != UINT64_MAX && next - scoreF < (uint64_t)span)
            span = (uint32_t)(next - scoreF);
        synth_render_span(s, busMix, pos, span);
        pos += span;
        scoreF += span;
    }
}

// ---------------------------------------------------------------------------
// Console helpers (values are tuning, lifted from the prototype console)
// ---------------------------------------------------------------------------

// per-layer strip trim, static reverb/delay send, canonical layer order
static const float STRIP_TRIM[6] = { 0.60f, 0.85f, 0.70f, 0.55f, 0.55f, 0.95f };
static const float SEND_REV[6]   = { 1.00f, 0.10f, 0.75f, 0.65f, 0.90f, 0.30f };
static const float SEND_DLY[6]   = { 0.00f, 0.00f, 1.00f, 0.30f, 0.80f, 0.00f };

// per-strip 3-band EQ: linear low/mid/high gains, shelf corner freqs
static const float STRIP_EQ[6][5] = {
    { 0.85f, 1.00f, 1.05f, 260.0f, 3200.0f }, // pad
    { 1.12f, 1.00f, 0.80f, 180.0f, 2200.0f }, // bass
    { 0.80f, 1.05f, 1.15f, 220.0f, 3600.0f }, // melody
    { 0.85f, 1.05f, 0.95f, 240.0f, 3000.0f }, // counter
    { 0.60f, 1.00f, 1.20f, 300.0f, 4800.0f }, // arp
    { 1.15f, 0.95f, 1.10f, 120.0f, 5000.0f }, // perc
};

// initial global send scales (bar-0 automation retargets immediately)
#define CONSOLE_REV_INIT 0.20f
#define CONSOLE_DLY_INIT 0.10f

uint32_t ano_synth_console_layout(AnoAudioBusDesc *out, uint32_t cap)
{
    if (!out || cap < ANO_SYNTH_CONSOLE_BUSES)
        return 0;
    memset(out, 0, ANO_SYNTH_CONSOLE_BUSES * sizeof *out);

    out[0].gain = 1.0f; // engine master: clip guard only

    out[ANO_SYNTH_BUS_MASTER] = (AnoAudioBusDesc){
        .parent = 0, .gain = 1.0f,
        .fx = { ANO_AUDIO_FX_DRIVE, ANO_AUDIO_FX_COMPRESSOR,
                ANO_AUDIO_FX_DCBLOCK, ANO_AUDIO_FX_LIMITER },
    };
    out[ANO_SYNTH_BUS_REVERB] = (AnoAudioBusDesc){
        .parent = ANO_SYNTH_BUS_MASTER, .gain = 1.0f,
        .fx = { ANO_AUDIO_FX_REVERB },
    };
    out[ANO_SYNTH_BUS_DELAY] = (AnoAudioBusDesc){
        .parent = ANO_SYNTH_BUS_MASTER, .gain = 0.7f, // prototype's delay-bus trim
        .fx = { ANO_AUDIO_FX_PINGPONG },
    };
    for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l) {
        AnoAudioBusDesc *b = &out[ANO_SYNTH_BUS_STRIP0 + l];
        b->parent = ANO_SYNTH_BUS_MASTER;
        b->gain   = STRIP_TRIM[l];
        b->fx[0]  = ANO_AUDIO_FX_EQ3;
        if (l == ANO_MUSIC_PAD) { // pad-only extras
            b->fx[1] = ANO_AUDIO_FX_CHORUS;
            b->fx[2] = ANO_AUDIO_FX_WIDTH;
        }
        b->sendTarget[0] = ANO_SYNTH_BUS_REVERB;
        b->sendLevel[0]  = SEND_REV[l] * CONSOLE_REV_INIT;
        if (SEND_DLY[l] > 0.0f) {
            b->sendTarget[1] = ANO_SYNTH_BUS_DELAY;
            b->sendLevel[1]  = SEND_DLY[l] * CONSOLE_DLY_INIT;
        }
    }
    // shimmer lane: dry at x0.2 through the fader, full-strength into the
    // reverb (post-fader send 5.0 x fader 0.2 = 1.0, the prototype's routing)
    out[ANO_SYNTH_BUS_SHIMMER] = (AnoAudioBusDesc){
        .parent = ANO_SYNTH_BUS_MASTER, .gain = 0.2f,
        .sendTarget = { ANO_SYNTH_BUS_REVERB }, .sendLevel = { 5.0f },
    };
    return ANO_SYNTH_CONSOLE_BUSES;
}

static AnoAudioOfflineEvent fx_evt(uint64_t frame, uint32_t bus, uint32_t slot,
                                   uint32_t param, float value)
{
    return (AnoAudioOfflineEvent){ .frame = frame, .cmd = {
        .kind = ACMD_FX_SET, .bus = bus, .fxSlot = slot,
        .paramId = param, .value = value } };
}

uint32_t ano_synth_console_setup(AnoAudioOfflineEvent *out, uint32_t cap)
{
    if (!out || cap < 64u)
        return 0;
    uint32_t n = 0;
    // master glue: bounded makeup 1.5 into the limiter; drive post-trim 0.7
    out[n++] = fx_evt(0, ANO_SYNTH_BUS_MASTER, 0, ANO_AUDIO_P_DRIVE_TRIM, 0.7f);
    out[n++] = fx_evt(0, ANO_SYNTH_BUS_MASTER, 1, ANO_AUDIO_P_COMP_MAKEUP, 1.5f);
    for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l) {
        uint32_t bus = ANO_SYNTH_BUS_STRIP0 + l;
        const float *eq = STRIP_EQ[l];
        float midF = sqrtf(eq[3] * eq[4]);
        out[n++] = fx_evt(0, bus, 0, ANO_AUDIO_P_EQ_LOW_GAIN_DB, 20.0f * log10f(eq[0]));
        out[n++] = fx_evt(0, bus, 0, ANO_AUDIO_P_EQ_LOW_FREQ, eq[3]);
        out[n++] = fx_evt(0, bus, 0, ANO_AUDIO_P_EQ_MID_GAIN_DB, 20.0f * log10f(eq[1]));
        out[n++] = fx_evt(0, bus, 0, ANO_AUDIO_P_EQ_MID_FREQ, midF);
        out[n++] = fx_evt(0, bus, 0, ANO_AUDIO_P_EQ_HIGH_GAIN_DB, 20.0f * log10f(eq[2]));
        out[n++] = fx_evt(0, bus, 0, ANO_AUDIO_P_EQ_HIGH_FREQ, eq[4]);
    }
    return n;
}

uint32_t ano_synth_console_automation(const AnoSynth *s, AnoAudioOfflineEvent *out,
                                      uint32_t cap)
{
    if (!s->scoreReady || s->live || !out || cap < s->barCount * 9u)
        return 0;
    uint32_t n = 0;
    for (uint32_t b = 0; b < s->barCount; ++b) {
        const AnoSynthBar *bar = &s->bars[b];
        const AnoMusicalParams *p = &bar->params;
        uint64_t f = bar->frame;
        for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l) {
            out[n++] = (AnoAudioOfflineEvent){ .frame = f, .cmd = {
                .kind = ACMD_BUS_SET, .bus = ANO_SYNTH_BUS_STRIP0 + l,
                .fields = ANO_AUDIO_FIELD_SEND0 | ANO_AUDIO_FIELD_SEND1,
                .send = { SEND_REV[l] * p->reverbSend, SEND_DLY[l] * p->delaySend } } };
        }
        float width = p->stereoWidth;
        if (width < 0.0f) width = 0.0f;
        if (width > 1.3f) width = 1.3f;
        out[n++] = fx_evt(f, ANO_SYNTH_BUS_STRIP0 + ANO_MUSIC_PAD, 2,
                          ANO_AUDIO_P_WIDTH_AMOUNT, width);
        out[n++] = fx_evt(f, ANO_SYNTH_BUS_MASTER, 0,
                          ANO_AUDIO_P_DRIVE_AMOUNT, 1.0f + p->drive * 4.0f);
        // tempo-synced dotted 8th, capped at the prototype's half-max-line
        float dotted = 0.75f * 60.0f / fmaxf((float)p->tempoBpm, 30.0f);
        out[n++] = fx_evt(f, ANO_SYNTH_BUS_DELAY, 0,
                          ANO_AUDIO_P_PP_TIME_MS, fminf(dotted, 0.7f) * 1000.0f);
    }
    return n;
}
