/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifdef __linux__

#include <anoptic_memory.h>
#include <mimalloc.h>

// Linux-specific implementation of aligned_malloc as defined in the ano_memory API.
inline void* ano_aligned_malloc(size_t size, size_t alignment) {
    return mi_malloc_aligned(size, alignment);
}

// Linux-specific implementation of aligned_free as defined in the ano_memory API.
inline void ano_aligned_free(void* ptr) {
    mi_free(ptr);
}

#endif
