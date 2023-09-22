/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifdef __linux__

#include "anoptic_memalign.h"
#include <stdlib.h>
#include <malloc.h> // for memalign


// Linux-specific implementation of aligned_malloc as defined in the anoptic_memory API.
void* anoptic_aligned_malloc(size_t size, size_t alignment) {
    void* ptr = memalign(alignment, size);
    return ptr;
}

// Linux-specific implementation of aligned_free as defined in the anoptic_memory API.
void anoptic_aligned_free(void* ptr) {
    free(ptr);  // free works for memory allocated by posix_memalign
}



#endif
