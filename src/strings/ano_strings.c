/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_strings.h"

#include <stdio.h>
#include <stdlib.h>
#include "anoptic_memory.h"

// Anoptic Strings Implementation
void anostr_cleanup(anostr_t *in) {
    // TODO: Add additional logic to check if it's actually managed by mimalloc.
    mi_free(in->buffer);
}


// --------------------------- TESTING ---------------------------
void intCleanup(const int *in) {

    printf("Cleanup function received number of value of: %d\n", *in);
}

typedef void ano_void;
typedef _BitInt(128) u128;

typedef struct {
    union {
        struct {
            uint64_t tdLo;
            uint64_t tdHi;
        };
        u128 tnd; // -IBM
    };
    uint64_t wpUUID;
    uint32_t irctdb;
    uint8_t wheels;
} dirle_chariot_t;

void stringAllocator(anostr_t *input) {
    input->len = 5;
    input->buffer = malloc(sizeof(char) * input->len);
    input->buffer[0] = 'M';
    input->buffer[0] = 'e';
    input->buffer[0] = 'o';
    input->buffer[0] = 'w';
    input->buffer[0] = '\0';
}

int autoStringTest() {

    uint8_t someBytes[1024];
    uint8_t* stackBytes = ano_salloc(42);

    if (true) {
        int intVar __attribute__((__cleanup__(intCleanup))) = 88; // This SIMPLETON here...
    }

    // Trvly scope-local and thread-local Heap
    if (true) {
        mi_heap_t *dirleHeap LOCALHEAPATTR = mi_heap_new();

        dirle_chariot_t *dirleChariots = mi_heap_zalloc_aligned(dirleHeap,
                                                    4096 * 8192 * sizeof(dirle_chariot_t),
                                                    sizeof(dirle_chariot_t));
        dirle_chariot_t **dirlePanzers = mi_heap_zalloc_aligned(dirleHeap,
                                                    4096 * sizeof(dirle_chariot_t),
                                                    sizeof(dirle_chariot_t));
        ano_void *dirleWagens = mi_heap_zalloc_aligned(dirleHeap,
                                                    4096 * sizeof(dirle_chariot_t),
                                                    sizeof(dirle_chariot_t));
        // Much like Wagen itself, which can mean a variety of things...
        // Non-pursuant to anything of... military value.

        dirle_chariot_t *firstOne = &dirleChariots[0];
        firstOne->tdLo = 0x1488148823237123;
        firstOne->tdHi = 0x1231488223444777;
        firstOne->wpUUID = 0x1488abab;
        firstOne->irctdb = 872282;
        firstOne->wheels = 8;
        printf("%llu\t%llu\n\n", firstOne->tdLo, firstOne->tdHi);
        printf("%llx\n", (unsigned long long)(firstOne->tnd & 0xFFFFFFFFFFFFFFFF));

        dirleChariots[1].tnd = 0x1488148813374444;
        dirleChariots[1].wpUUID = 0x1488abac;
        dirleChariots[1].irctdb = 2222;
        dirleChariots[1].wheels = 6;
        printf("%llu\t%llu\n\n", dirleChariots[1].tdLo, dirleChariots[1].tdHi);

        srand(0x1488);
        for (uint32_t i = 2; i < 4096; i++) {
            dirleChariots[i].tdLo = rand();
            dirleChariots[i].tdHi = rand();
            dirleChariots[i].wpUUID = rand();
            dirleChariots[i].irctdb = rand() % 40000;
            dirleChariots[i].wheels = rand() % 16;
        }
        printf("All chariots released from hell.");

    }

    anostr_t meow = {"abcdefhijklmnop\0", 16};
    //ANOSTR_HEAP_UTFSLICE(meow, 2, 5); // TODO: this might be quite interesting...
    //ANOSTR_STACK_UTFSLICE(meow, 2, 5);

    printf("\n\n\nKALI I CALL ON THEE\n\n\n");

    return 0;
}
