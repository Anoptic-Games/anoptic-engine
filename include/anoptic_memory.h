//
// Created by Pyrus on 2024-01-12.
//

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_H

#include <stddef.h> // for size_t
#include <mimalloc.h>
#if !defined(__APPLE__)
#include <malloc.h> // is this ever necessary if we supply mimalloc-override.h?
#endif
#include <mimalloc-override.h>
#if defined(__linux__) || defined(__APPLE__)
#include <alloca.h>
#endif

// Hardware interference sizes. Compile-time constants: _Alignas and struct layout need a
// constant, not a runtime cache query. ANO_CACHE_LINE is the true coherency line — the grain for
// data meant to share a line (packing, cache-line-granular reservation). ANO_THREAD_LINE is the
// false-sharing isolation distance — _Alignas hot per-thread atomics to it so two cores' cursors
// never collide. 128 on every target: Apple Silicon's line is 128, and x86-64's adjacent-line
// prefetcher moves the 128-byte buddy pair as one, so 64-byte separation still ping-pongs.
// Override either with -DANO_CACHE_LINE=N / -DANO_THREAD_LINE=N.
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

// Destroys and frees the pages of a mimalloc local heap.
// Automatically called at end of scope by variables declared with LOCALHEAPATTR
void ano_heap_release(mi_heap_t **in);

// Attribute to be used in conjunction with mi_heap_new() to make a scoped heap.
// Usage: mi_heap_t *exampleHeap LOCALHEAPATTR = mi_heap_new();
#define LOCALHEAPATTR  __attribute__((__cleanup__(ano_heap_release)))

// Allocates a block of memory on the stack.
// Warning: Use with EXTREME care to not overflow!
#define ano_salloc(bytes) alloca((size_t)bytes)

/**
 * @brief Allocates @p size bytes aligned to a @p alignment boundary.
 *
 * @param size      Block size in bytes.
 * @param alignment Block alignment, a power of 2.
 *
 * @return Pointer to the block, or NULL on failure.
 *
 * @note Returns NULL if @p size or @p alignment is 0.
 */
void* ano_aligned_malloc(size_t size, size_t alignment);

/**
 * @brief Frees an aligned block.
 *
 * @param ptr Block to free.
 *
 * @note Undefined behavior to free a block not from ano_aligned_malloc.
 */
void ano_aligned_free(void* ptr);

#endif //ANOPTICENGINE_ANOPTIC_MEMORY_H