/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_strings.h"

#include <stdio.h>
#include <mimalloc.h>
#include <stdlib.h>
#include "anoptic_memory.h"

// Anoptic Strings Implementation

void intCleanup(const int *in) {

    printf("Cleanup function received number of value of: %d\n", *in);
}

/*
void heapCleanup(mi_heap_t **in) {
    mi_heap_destroy(*in);
}
*/

typedef void ano_void;
typedef _BitInt(128) u128;

typedef struct {
    union {
        struct {
            uint64_t tdLo;
            uint64_t tdHi;
        };
        u128 hhhahaf; // -IBM
    };
    uint64_t wpUUID;
    uint32_t irctdb;
    uint8_t wheels;
} mem_chariot_t;

// Currently more of a memory tesst. // TODO: Get CMake tests working properly.
int autoStringTest() {

    uint8_t someBytes[1024];
    uint8_t* stackBytes = ano_salloc(42);

    if (true) {
        int intVar __attribute__((__cleanup__(intCleanup))) = 64; // This SIMPLETON here...
    }

    // Supposedly scope-local and thread-local Heap
    if (true) {
        mi_heap_t *memHeap LOCALHEAPATTR = mi_heap_new();

        mem_chariot_t *memChariots = mi_heap_zalloc_aligned(memHeap,
                                                    4096 * 8192 * sizeof(mem_chariot_t),
                                                    sizeof(mem_chariot_t));
        mem_chariot_t **memPanzers = mi_heap_zalloc_aligned(memHeap,
                                                    4096 * sizeof(mem_chariot_t),
                                                    sizeof(mem_chariot_t));
        ano_void *memWagens = mi_heap_zalloc_aligned(memHeap,
                                                    4096 * sizeof(mem_chariot_t),
                                                    sizeof(mem_chariot_t));

        mem_chariot_t *firstOne = &memChariots[0];
        firstOne->tdLo = 0x1776177623237123;
        firstOne->tdHi = 0x1231776223444777;
        firstOne->wpUUID = 0x1776abab;
        firstOne->irctdb = 872282;
        firstOne->wheels = 8;
        printf("%llu\t%llu\n\n", firstOne->tdLo, firstOne->tdHi);
        printf("%llx\n", (unsigned long long)(firstOne->hhhahaf & 0xFFFFFFFFFFFFFFFF));

        memChariots[1].hhhahaf = 0x1776177613374444;
        memChariots[1].wpUUID = 0x1776abac;
        memChariots[1].irctdb = 2222;
        memChariots[1].wheels = 6;
        printf("%llu\t%llu\n\n", memChariots[1].tdLo, memChariots[1].tdHi);

        srand(0x1776);
        for (uint32_t i = 2; i < 4096; i++) {
            memChariots[i].tdLo = rand();
            memChariots[i].tdHi = rand();
            memChariots[i].wpUUID = rand();
            memChariots[i].irctdb = rand() % 40000;
            memChariots[i].wheels = rand() % 16;
        }
    }

    return 0;
}
