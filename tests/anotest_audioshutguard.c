/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: adopted sample blocks orphaned by ano_audio_shutdown (ano_audio.c:204, docs/BUGS.md,
// Audio / Implementation). Adopted blocks live on the DEFAULT mi heap (plain mi_malloc,
// ano_audio.c:260), not the module heap, and the module promises them lossless: they ride home in
// AEVT_BUFFER_RETIRED (audio_internal.h:9; anoptic_audio.h:326). Shutdown joins the mixer, stops
// the device, destroys both bridge rings and the module heap 〜 but never walks mx->buffers or
// drains a ring, so LIVE owned blocks, ACMD_BUFFER_REGISTER blocks still queued in the command
// ring, and un-polled AEVT_BUFFER_RETIRED blocks all leak permanently; the producer can never poll
// a destroyed world to get them back.
// Harness: compiles the REAL ano_audio.c + audio_mixer.c TUs into this executable with their
// allocator tokens (mi_malloc/mi_free) interposed to audit adopted-block balance. Everything else
// (fx, null device, log, threads, time) resolves from anoptic_core; the archive's copies of the
// two TUs are never pulled because every one of their symbols is already defined here. Null
// backend pinned 〜 no sound hardware.
// Controls prove a full register -> release -> poll -> block_free round-trip balances the seam to
// zero and that a discharged world shuts down clean, so a fix cannot pass by never adopting or by
// rejecting registration. The triggers ask only that shutdown leave no adopted block live 〜
// fix-agnostic: freeing owned slots at teardown, draining both rings, or any equivalent discharge
// all pass. Exit 0 == pass.

#include <anoptic_audio.h>
#include <anoptic_time.h>

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Allocator seam 〜 the -Dmi_*=anotest_seam_* tokens rename every declaration the headers made,
   so the real allocator entry points are redeclared here and the seams forward to them. */

#undef mi_malloc
#undef mi_free
void *mi_malloc(size_t size);
void  mi_free(void *p);

// Live-set tracker over the audio lifecycle TUs' default-heap blocks. buffer_reject may free on
// the mixer thread, so a flag spinlock guards the set. Bounded; overflow counts as failure.
#define TRACK_MAX 16
static void      *g_live[TRACK_MAX];
static size_t     g_liveBytes[TRACK_MAX];
static uint32_t   g_liveCount;
static bool       g_trackOverflow;
static atomic_flag g_trackLock = ATOMIC_FLAG_INIT;

static void track_lock(void)   { while (atomic_flag_test_and_set_explicit(&g_trackLock, memory_order_acquire)) {} }
static void track_unlock(void) { atomic_flag_clear_explicit(&g_trackLock, memory_order_release); }

static void track_add(void *p, size_t bytes)
{
    track_lock();
    if (g_liveCount >= TRACK_MAX) { g_trackOverflow = true; track_unlock(); return; }
    g_live[g_liveCount] = p;
    g_liveBytes[g_liveCount] = bytes;
    g_liveCount++;
    track_unlock();
}

static void track_remove(void *p)
{
    track_lock();
    for (uint32_t i = 0; i < g_liveCount; i++) {
        if (g_live[i] != p)
            continue;
        g_liveCount--;
        g_live[i] = g_live[g_liveCount];
        g_liveBytes[i] = g_liveBytes[g_liveCount];
        break;
    }
    track_unlock();
}

// in: n bytes  out: fresh block, tracked
void *anotest_seam_malloc(size_t n)
{
    void *p = mi_malloc(n);
    if (p) track_add(p, n);
    return p;
}

// in: block or NULL  out: forwarded to the real free, untracked
void anotest_seam_free(void *p)
{
    if (p) track_remove(p);
    mi_free(p);
}

// Prints every still-live adopted block, returns the count.
static uint32_t leaked_count(void)
{
    track_lock();
    uint32_t n = g_liveCount;
    for (uint32_t i = 0; i < n; i++)
        printf("  orphaned adopted block %p (%" PRIuMAX " B)\n", g_live[i], (uintmax_t)g_liveBytes[i]);
    track_unlock();
    return n;
}


/* World helpers 〜 null backend, telemetry-paced waits */

// out: true once telemetry reports blockIndex >= target within timeoutMs
static bool wait_blocks(AnoAudioBridge *b, uint64_t target, uint32_t timeoutMs)
{
    uint32_t start = ano_timestamp_ms();
    for (;;) {
        AnoAudioTelemetry t;
        if (ano_audio_acquire_telemetry(b, &t) && t.blockIndex >= target)
            return true;
        if (ano_timestamp_ms() - start > timeoutMs)
            return false;
        ano_sleep(2000);
    }
}

// out: latest published blockIndex, 0 before the first publish
static uint64_t cur_block(AnoAudioBridge *b)
{
    AnoAudioTelemetry t;
    return ano_audio_acquire_telemetry(b, &t) ? t.blockIndex : 0u;
}

// Drains events until AEVT_BUFFER_RETIRED for id lands. out: its block via *outBlock.
static bool poll_retired(AnoAudioBridge *b, uint32_t id, uint32_t timeoutMs, void **outBlock)
{
    uint32_t start = ano_timestamp_ms();
    for (;;) {
        AnoAudioEvent e;
        while (ano_audio_poll_event(b, &e)) {
            if (e.kind == AEVT_BUFFER_RETIRED && e.u.buffer.buffer_id == id) {
                *outBlock = e.u.buffer.block;
                return true;
            }
        }
        if (ano_timestamp_ms() - start > timeoutMs)
            return false;
        ano_sleep(2000);
    }
}

// Fresh null-backend world. out: bridge, or NULL on init failure.
static AnoAudioBridge *world_up(void)
{
    AnoAudioConfig cfg = { .backend = ANO_AUDIO_BACKEND_NULL_DEV };
    if (!ano_audio_init(&cfg))
        return NULL;
    return anoAudioBridge();
}

static float g_pcm[256]; // mono frames; silence is a legal sample buffer


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // control cycle: the lossless round-trip the module documents, audited through the seam 〜
    // register adopts a tracked block, release rides it home, block_free balances to zero
    AnoAudioBridge *b = world_up();
    CHECK(b != NULL, "control: null-backend world up");
    if (!b) return 1;
    CHECK(wait_blocks(b, 3u, 5000u), "control: mixer heartbeat");
    CHECK(ano_audio_buffer_register(b, 1u, g_pcm, 256u, 1u), "control: buffer 1 registered");
    CHECK(g_liveCount == 1u, "control: seam tracked the adopted block");
    CHECK(wait_blocks(b, cur_block(b) + 4u, 5000u), "control: registration applied at a block boundary");
    CHECK(ano_audio_buffer_release(b, 1u), "control: buffer 1 release submitted");
    void *home = NULL;
    CHECK(poll_retired(b, 1u, 5000u, &home), "control: AEVT_BUFFER_RETIRED rode home");
    CHECK(home != NULL, "control: retirement event carries the block");
    ano_audio_block_free(home);
    CHECK(g_liveCount == 0u, "control: lossless round-trip balances the seam to zero");
    ano_audio_shutdown();
    CHECK(g_liveCount == 0u, "control: a fully discharged world shuts down balanced");
    CHECK(!g_trackOverflow, "control: allocation tracker within bounds");

    // trigger 1: shutdown with a LIVE adopted block resident 〜 the invariant says it must be
    // discharged (returned or freed); today the module heap dies and the block orphans
    printf("trigger: shutdown with a LIVE adopted block resident\n");
    b = world_up();
    CHECK(b != NULL, "trigger 1: world up");
    if (!b) return 1;
    CHECK(ano_audio_buffer_register(b, 2u, g_pcm, 256u, 1u), "trigger 1: buffer 2 registered");
    CHECK(wait_blocks(b, cur_block(b) + 4u, 5000u), "trigger 1: adoption applied before shutdown");
    ano_audio_shutdown();
    uint32_t leakedLive = leaked_count();
    CHECK(leakedLive == 0u, "trigger: shutdown must discharge LIVE adopted blocks (ano_audio.c:204 frees the module heap, never the mi_malloc'd adoptions)");

    // trigger 2: shutdown with an emitted-but-unpolled retirement 〜 the block sits in the events
    // ring the shutdown destroys undrained
    printf("trigger: shutdown with an un-polled AEVT_BUFFER_RETIRED in the ring\n");
    b = world_up();
    CHECK(b != NULL, "trigger 2: world up");
    if (!b) return 1;
    CHECK(ano_audio_buffer_register(b, 3u, g_pcm, 256u, 1u), "trigger 2: buffer 3 registered");
    CHECK(wait_blocks(b, cur_block(b) + 4u, 5000u), "trigger 2: adoption applied");
    CHECK(ano_audio_buffer_release(b, 3u), "trigger 2: release submitted");
    CHECK(wait_blocks(b, cur_block(b) + 4u, 5000u), "trigger 2: retirement pass ran before shutdown");
    ano_audio_shutdown();
    uint32_t leakedTotal = leaked_count();
    CHECK(leakedTotal - leakedLive == 0u, "trigger: shutdown must discharge un-polled AEVT_BUFFER_RETIRED blocks (bridge rings destroyed undrained)");

    if (failures) {
        printf("anotest_audioshutguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_audioshutguard: all passed\n");
    return 0;
}
