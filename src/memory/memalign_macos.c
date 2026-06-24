/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__APPLE__)

#include <anoptic_memory.h>
#include <mimalloc.h>

// macOS ano_aligned_malloc.
inline void* ano_aligned_malloc(size_t size, size_t alignment) {
    return mi_malloc_aligned(size, alignment);  // Good
}

// macOS ano_aligned_free.
inline void ano_aligned_free(void* ptr) {
    mi_free(ptr);
}

#endif
