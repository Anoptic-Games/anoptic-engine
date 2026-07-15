/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Runtime interning table: one canonical copy per distinct string, dense u32 symbols.
// Open addressing over cached 64-bit hashes. Dense per-symbol arrays; growth rehashes slots only.
// Everything allocates from the table's heap and dies with it. No destroy. Single mutator.

#include "strings/ano_strings_internal.h"

#define INTERN_INITIAL_SLOTS 64u    // power of two; grows at 70% load

// struct anostr_intern_t lives in ano_strings_internal.h.

anostr_intern_t *anostr_intern_make(mi_heap_t *heap)
{
    if (heap == NULL)
        return NULL;
    anostr_intern_t *t = mi_heap_zalloc(heap, sizeof *t);
    if (t == NULL)
        return NULL;
    t->slots = mi_heap_zalloc(heap, INTERN_INITIAL_SLOTS * sizeof *t->slots);
    if (t->slots == NULL) {
        mi_free(t);
        return NULL;
    }
    t->heap = heap;
    t->slotMask = INTERN_INITIAL_SLOTS - 1;
    return t;
}

// Symbol holding (hash, s), or ANOSTR_SYM_NONE at the first empty slot.
static anostr_sym probe_find(const anostr_intern_t *t, uint64_t hash, anostr_t s)
{
    uint32_t idx = (uint32_t)hash & t->slotMask;
    while (t->slots[idx] != 0) {
        anostr_sym sym = t->slots[idx] - 1;
        if (t->hashes[sym] == hash && anostr_eq(t->strs[sym], s))
            return sym;
        idx = (idx + 1) & t->slotMask;
    }
    return ANOSTR_SYM_NONE;
}

static void slot_insert(uint32_t *slots, uint32_t mask, uint64_t hash, anostr_sym sym)
{
    uint32_t idx = (uint32_t)hash & mask;
    while (slots[idx] != 0)
        idx = (idx + 1) & mask;
    slots[idx] = sym + 1;
}

// Double the slot table and reinsert from cached hashes -- no string bytes touched.
static int grow_slots(anostr_intern_t *t)
{
    uint64_t newCap = ((uint64_t)t->slotMask + 1) * 2;
    if (newCap > UINT32_MAX)
        return -1;
    uint32_t *fresh = mi_heap_zalloc(t->heap, (size_t)newCap * sizeof *fresh);
    if (fresh == NULL)
        return -1;
    uint32_t newMask = (uint32_t)newCap - 1;
    for (anostr_sym sym = 0; sym < t->count; sym++)
        slot_insert(fresh, newMask, t->hashes[sym], sym);
    mi_free(t->slots);
    t->slots = fresh;
    t->slotMask = newMask;
    return 0;
}

static int grow_arrays(anostr_intern_t *t)
{
    uint64_t newCap = t->arrCap ? (uint64_t)t->arrCap * 2 : 32;
    if (newCap > UINT32_MAX)
        return -1;
    uint64_t *hashes = mi_heap_realloc(t->heap, t->hashes, (size_t)newCap * sizeof *hashes);
    if (hashes == NULL)
        return -1;
    t->hashes = hashes;    // committed independently
    anostr_t *strs = mi_heap_realloc(t->heap, t->strs, (size_t)newCap * sizeof *strs);
    if (strs == NULL)
        return -1;         // arrCap unchanged; larger hashes block is slack
    t->strs = strs;
    t->arrCap = (uint32_t)newCap;
    return 0;
}

anostr_sym anostr_intern(anostr_intern_t *t, anostr_t s)
{
    if (t == NULL)
        return ANOSTR_SYM_NONE;

    uint64_t hash = anostr_hash(s);
    anostr_sym found = probe_find(t, hash, s);
    if (found != ANOSTR_SYM_NONE)
        return found;

    // Insert path. Grow first so failure leaves the table as it was.
    if (t->count >= UINT32_MAX - 1)     // sym + 1 must fit a slot; NONE stays reserved
        return ANOSTR_SYM_NONE;
    if ((uint64_t)(t->count + 1) * 10 > ((uint64_t)t->slotMask + 1) * 7 && grow_slots(t) != 0)
        return ANOSTR_SYM_NONE;
    if (t->count == t->arrCap && grow_arrays(t) != 0)
        return ANOSTR_SYM_NONE;

    anostr_t canonical = anostr_keep(t->heap, s);
    if (canonical.len != s.len)         // keep fail: long copy failed
        return ANOSTR_SYM_NONE;

    anostr_sym sym = t->count++;
    t->hashes[sym] = hash;
    t->strs[sym] = canonical;
    slot_insert(t->slots, t->slotMask, hash, sym);
    return sym;
}

anostr_sym anostr_intern_find(const anostr_intern_t *t, anostr_t s)
{
    if (t == NULL)
        return ANOSTR_SYM_NONE;
    return probe_find(t, anostr_hash(s), s);
}

anostr_t anostr_sym_str(const anostr_intern_t *t, anostr_sym sym)
{
    if (t == NULL || sym >= t->count)
        return anostr_empty();
    return t->strs[sym];
}

anostr_t anostr_dedupe(anostr_intern_t *t, anostr_t s)
{
    anostr_sym sym = anostr_intern(t, s);
    if (sym == ANOSTR_SYM_NONE)
        return s;   // table unavailable: caller's value still usable
    return t->strs[sym];
}

size_t anostr_intern_count(const anostr_intern_t *t)
{
    return t == NULL ? 0 : t->count;
}
