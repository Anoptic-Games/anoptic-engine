/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_H

#include <mimalloc.h>
#include <malloc.h>
#include <mimalloc-override.h>
#ifdef __LINUX__
#include <alloca.h>
#endif

// Destroys and frees the pages of a mimalloc local heap.
// Automatically called at end of scope by variables declared with LOCALHEAPATTR
void ano_heap_release(mi_heap_t **in);

// Attribute to be used in conjunction with mi_heap_new() to make a scoped heap.
// Usage: mi_heap_t *exampleHeap LOCALHEAPATTR = mi_heap_new();
#define LOCALHEAPATTR  __attribute__((__cleanup__(ano_heap_release)))

// An alias to the __cleanup__ attribute, where 'FUNC' is the name of the user-defined function to be called when leaving scope.
#define CLEANUPATTR(FUNC) __attribute__((__cleanup__(FUNC)))

// Allocates a block of memory on the stack.
// Warning: Use with EXTREME care to not overflow!
#define ano_salloc(bytes) alloca((size_t)bytes)

#endif //ANOPTICENGINE_ANOPTIC_MEMORY_H