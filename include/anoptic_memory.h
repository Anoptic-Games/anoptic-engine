/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_H

#include <stddef.h>
#include <stdlib.h> // before mimalloc-override.h (MinGW _msize/_aligned_msize)
#include <mimalloc.h>
#if !defined(__APPLE__)
#include <malloc.h>
#endif
#include <mimalloc-override.h>
#if defined(__linux__) || defined(__APPLE__)
#include <alloca.h>
#endif

// ANO_CACHE_LINE: coherency grain (default 128 Apple aarch64, else 64).
// ANO_THREAD_LINE: false-sharing isolation for hot per-thread atomics (default 128).
// Compile-time only. Override: -DANO_CACHE_LINE=N / -DANO_THREAD_LINE=N.
#ifndef ANO_CACHE_LINE
#if defined(__APPLE__) && defined(__aarch64__)
#define ANO_CACHE_LINE 128
#else
#define ANO_CACHE_LINE 64       // x86-64 and generic arm64
#endif
#endif
#ifndef ANO_THREAD_LINE
#define ANO_THREAD_LINE 128
#endif

// Destroys a mimalloc local heap. Fired at end of scope for LOCALHEAPATTR vars.
void ano_heap_release(mi_heap_t **in);

// Scoped heap: mi_heap_t *h LOCALHEAPATTR = mi_heap_new();
#define LOCALHEAPATTR  __attribute__((__cleanup__(ano_heap_release)))

// Stack alloc. Overflow risk.
#define ano_salloc(bytes) alloca((size_t)bytes)

// Allocates size bytes aligned to alignment (power of 2). mi_malloc_aligned wrapper.
// Returns pointer, or NULL on failure.
void* ano_aligned_malloc(size_t size, size_t alignment);

// Frees a block from ano_aligned_malloc. Else UB.
void ano_aligned_free(void* ptr);

#endif //ANOPTICENGINE_ANOPTIC_MEMORY_H
