/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifdef _WIN64

#include "anoptic_memalign.h"
#include <malloc.h>

// Windows-specific implementation of aligned_malloc as defined in the ano_memory API.
void* ano_aligned_malloc(size_t size, size_t alignment) {
    return _aligned_malloc(size, alignment);
}

// Windows-specific implementation of aligned_free as defined in the ano_memory API.
void ano_aligned_free(void* ptr) {
    _aligned_free(ptr);
}



#endif